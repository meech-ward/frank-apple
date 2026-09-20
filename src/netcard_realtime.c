/*
 * netcard_realtime.c - Supabase Realtime typing channel (slot 1, reg 5).
 *
 * A second, persistent TLS connection alongside the REST fetch engine in
 * mii_netcard.c. It joins a Supabase Realtime broadcast channel over a
 * WebSocket (Phoenix v1) and feeds received command text into the
 * emulated keyboard via typing_push(), one byte per frame.
 *
 * State machine, driven from netcard_poll() with time_us_32():
 *   DOWN (link down or no host: wait) -> RESOLVING (DNS) ->
 *   CONNECTING (TCP+TLS handshake) -> UPGRADING (HTTP 101) ->
 *   JOINING (phx_join sent) -> JOINED (heartbeat every 25 s).
 * Any error, close frame, err callback, or 30 s without a heartbeat
 * reply while JOINED tears the pcb down (same detach-then-close/abort
 * discipline as the fetch code) and goes to BACKOFF for 5 s before
 * RESOLVING again. Never blocks; never sleeps.
 *
 * With an empty NETCARD_RT_HOST (built without secrets) the client
 * stays idle in DOWN. The key and the host are never logged.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pico/time.h"
#include "pico/rand.h"

#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "mbedtls/ssl.h"
#include "mbedtls/base64.h"

#include "debug_log.h"
#include "netcard.h"
#include "netcard_config.h"
#include "typing.h"

#if !NETCARD_REALTIME
#error "netcard_realtime.c needs NETCARD_REALTIME=1"
#endif

#define RT_PORT 443
#define RT_RX_SIZE 4096
#define RT_TX_SIZE 2048
#define RT_REQ_SIZE 1024
#define RT_JOIN_SIZE 512
#define RT_MAX_FRAME 2048
#define RT_TEXT_MAX 512
#define RT_HB_INTERVAL_US (25u * 1000u * 1000u)
#define RT_HB_TIMEOUT_US (30u * 1000u * 1000u)
#define RT_BACKOFF_US (5u * 1000u * 1000u)

enum {
    RT_DOWN = 0,
    RT_RESOLVING,
    RT_CONNECTING,
    RT_UPGRADING,
    RT_JOINING,
    RT_JOINED,
    RT_BACKOFF,
};

typedef struct {
    int state;
    struct altcp_pcb *pcb;
    uint8_t rx[RT_RX_SIZE]; /* bytes kept across recv callbacks */
    uint32_t rx_len;
    uint8_t tx[RT_TX_SIZE]; /* one masked client frame */
    char req[RT_REQ_SIZE];  /* HTTP upgrade request */
    char join[RT_JOIN_SIZE]; /* phx_join / heartbeat JSON */
    uint32_t ref;           /* Phoenix ref counter */
    uint32_t last_hb_send;
    uint32_t last_hb_reply;
    uint32_t backoff_until;
} rt_state_t;

static rt_state_t s_rt;

static const char *
rt_state_name(int s)
{
    switch (s) {
        case RT_DOWN: return "DOWN";
        case RT_RESOLVING: return "RESOLVING";
        case RT_CONNECTING: return "CONNECTING";
        case RT_UPGRADING: return "UPGRADING";
        case RT_JOINING: return "JOINING";
        case RT_JOINED: return "JOINED";
        case RT_BACKOFF: return "BACKOFF";
        default: return "?";
    }
}

static void
rt_set_state(int s)
{
    if (s_rt.state == s)
        return;
    MII_DEBUG_PRINTF("realtime: %s -> %s\n",
            rt_state_name(s_rt.state), rt_state_name(s));
    s_rt.state = s;
}

/* Detach callbacks, then free the pcb. close_ok selects close vs abort. */
static void
rt_pcb_teardown(bool close_ok)
{
    struct altcp_pcb *pcb = s_rt.pcb;
    s_rt.pcb = NULL;
    if (pcb == NULL)
        return;
    altcp_arg(pcb, NULL);
    altcp_recv(pcb, NULL);
    altcp_err(pcb, NULL);
    altcp_poll(pcb, NULL, 0);
    if (close_ok) {
        if (altcp_close(pcb) != ERR_OK)
            altcp_abort(pcb);
    } else {
        altcp_abort(pcb);
    }
}

static void
rt_backoff(void)
{
    rt_pcb_teardown(false);
    s_rt.rx_len = 0;
    s_rt.backoff_until = time_us_32() + RT_BACKOFF_US;
    rt_set_state(RT_BACKOFF);
}

/* Bounded substring search: the frame payload is not NUL-terminated. */
static bool
rt_contains(const uint8_t *hay, uint32_t hlen, const char *needle)
{
    uint32_t nlen = (uint32_t)strlen(needle);
    uint32_t i;
    if (nlen == 0 || hlen < nlen)
        return false;
    for (i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hay + i, needle, nlen) == 0)
            return true;
    }
    return false;
}

static bool
rt_is_ws(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/*
 * Extract the command text from a broadcast frame body without a JSON
 * library: find "text", skip whitespace/colon/whitespace, expect the
 * opening quote, then copy up to the closing unescaped quote. Handles
 * \" \\ \/ \n \r \t (\n and \r become 0x0D) and \uXXXX (emits ?).
 * Returns the byte count, or -1 when there is no command text.
 */
static int
rt_extract_text(const uint8_t *p, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t i = 0;
    uint32_t olen = 0;
    bool found = false;
    bool closed = false;
    for (i = 0; i + 6 <= n; i++) {
        if (memcmp(p + i, "\"text\"", 6) == 0) {
            i += 6;
            found = true;
            break;
        }
    }
    if (!found)
        return -1;
    while (i < n && rt_is_ws(p[i]))
        i++;
    if (i >= n || p[i] != ':')
        return -1;
    i++;
    while (i < n && rt_is_ws(p[i]))
        i++;
    if (i >= n || p[i] != '"')
        return -1;
    i++;
    while (i < n) {
        uint8_t c = p[i++];
        uint8_t e;
        uint8_t v;
        int k;
        if (c == '"') {
            closed = true;
            break;
        }
        if (c != '\\') {
            if (olen < cap)
                out[olen++] = c;
            continue;
        }
        if (i >= n)
            break;
        e = p[i++];
        switch (e) {
            case '"': v = '"'; break;
            case '\\': v = '\\'; break;
            case '/': v = '/'; break;
            case 'n':
            case 'r': v = 0x0D; break;
            case 't': v = '\t'; break;
            case 'u':
                for (k = 0; k < 4 && i < n; k++)
                    i++;
                v = '?';
                break;
            default: v = e; break;
        }
        if (olen < cap)
            out[olen++] = v;
    }
    return closed ? (int)olen : -1;
}

static void
rt_on_text(const uint8_t *p, uint32_t n, uint32_t now)
{
    static uint8_t text[RT_TEXT_MAX]; /* off the 2 KB main stack: this runs inside the TLS poll path */
    int tlen;
    if (s_rt.state == RT_JOINING) {
        if (rt_contains(p, n, "phx_reply")) {
            rt_set_state(RT_JOINED);
            s_rt.last_hb_send = now;
            s_rt.last_hb_reply = now;
        }
        return;
    }
    if (s_rt.state != RT_JOINED)
        return;
    if (rt_contains(p, n, "phx_reply"))
        s_rt.last_hb_reply = now;
    if (!rt_contains(p, n, "\"event\":\"broadcast\""))
        return;
    tlen = rt_extract_text(p, n, text, sizeof(text));
    if (tlen < 0)
        return;
    MII_DEBUG_PRINTF("realtime: broadcast %d bytes\n", tlen);
    typing_push(text, (size_t)tlen);
    typing_push((const uint8_t *)"\r", 1);
}

/* Send one masked client frame from the static tx buffer. */
static err_t
rt_ws_send(uint8_t opcode, const uint8_t *data, uint32_t len)
{
    uint8_t *b = s_rt.tx;
    uint8_t mask[4];
    uint32_t m;
    uint32_t hdr;
    uint32_t i;
    err_t e;
    if (s_rt.pcb == NULL)
        return ERR_CLSD;
    if (len <= 125) {
        b[0] = (uint8_t)(0x80 | opcode);
        b[1] = (uint8_t)(0x80 | len);
        hdr = 2;
    } else {
        if (len > RT_TX_SIZE - 8)
            return ERR_MEM;
        b[0] = (uint8_t)(0x80 | opcode);
        b[1] = (uint8_t)(0x80 | 126);
        b[2] = (uint8_t)((len >> 8) & 0xFF);
        b[3] = (uint8_t)(len & 0xFF);
        hdr = 4;
    }
    m = get_rand_32();
    memcpy(mask, &m, 4);
    memcpy(b + hdr, mask, 4);
    for (i = 0; i < len; i++)
        b[hdr + 4 + i] = (uint8_t)(data[i] ^ mask[i % 4]);
    e = altcp_write(s_rt.pcb, b, (u16_t)(hdr + 4 + len),
            TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK)
        return e;
    altcp_output(s_rt.pcb);
    return ERR_OK;
}

static err_t
rt_send_join(void)
{
    int len;
    s_rt.ref = 1;
    len = snprintf(s_rt.join, sizeof(s_rt.join),
        "{\"topic\":\"realtime:%s\",\"event\":\"phx_join\","
        "\"payload\":{\"config\":{\"broadcast\":{\"ack\":false,\"self\":false},"
        "\"presence\":{\"enabled\":false},\"postgres_changes\":[],"
        "\"private\":false}},\"ref\":\"%lu\"}",
        NETCARD_RT_TOPIC, (unsigned long)s_rt.ref);
    if (len < 0 || len >= (int)sizeof(s_rt.join))
        return ERR_MEM;
    return rt_ws_send(0x1, (const uint8_t *)s_rt.join, (uint32_t)len);
}

static err_t
rt_send_heartbeat(void)
{
    int len;
    s_rt.ref++;
    len = snprintf(s_rt.join, sizeof(s_rt.join),
        "{\"topic\":\"phoenix\",\"event\":\"heartbeat\","
        "\"payload\":{},\"ref\":\"%lu\"}",
        (unsigned long)s_rt.ref);
    if (len < 0 || len >= (int)sizeof(s_rt.join))
        return ERR_MEM;
    return rt_ws_send(0x1, (const uint8_t *)s_rt.join, (uint32_t)len);
}

static void
rt_consume(uint32_t n)
{
    uint32_t rest = s_rt.rx_len - n;
    memmove(s_rt.rx, s_rt.rx + n, rest);
    s_rt.rx_len = rest;
}

/* Offset just past the "\r\n\r\n" header end, or -1 when absent. */
static int
rt_header_end(void)
{
    uint32_t i;
    for (i = 0; i + 4 <= s_rt.rx_len; i++) {
        if (s_rt.rx[i] == '\r' && s_rt.rx[i + 1] == '\n' &&
            s_rt.rx[i + 2] == '\r' && s_rt.rx[i + 3] == '\n')
            return (int)(i + 4);
    }
    return -1;
}

/*
 * Parse complete frames from the head of the receive buffer in a loop.
 * A frame split across recv callbacks waits for more bytes; a fragment
 * or a frame over 2048 bytes is dropped and parsing continues.
 * Returns true when the connection was torn down (caller: ERR_ABRT).
 */
static bool
rt_parse(uint32_t now)
{
    if (s_rt.state == RT_UPGRADING) {
        int hdr = rt_header_end();
        uint32_t rest;
        if (hdr < 0) {
            if (s_rt.rx_len >= RT_RX_SIZE) {
                MII_DEBUG_PRINTF("realtime: upgrade header too long\n");
                rt_backoff();
                return true;
            }
            return false;
        }
        if (s_rt.rx_len < 12 ||
                memcmp(s_rt.rx, "HTTP/1.1 101", 12) != 0) {
            MII_DEBUG_PRINTF("realtime: upgrade rejected\n");
            rt_backoff();
            return true;
        }
        rest = s_rt.rx_len - (uint32_t)hdr;
        memmove(s_rt.rx, s_rt.rx + hdr, rest);
        s_rt.rx_len = rest;
        if (rt_send_join() != ERR_OK) {
            MII_DEBUG_PRINTF("realtime: join send failed\n");
            rt_backoff();
            return true;
        }
        rt_set_state(RT_JOINING);
    }
    if (s_rt.state != RT_JOINING && s_rt.state != RT_JOINED)
        return false;
    for (;;) {
        uint8_t *r = s_rt.rx;
        bool fin;
        uint8_t op;
        bool masked;
        uint64_t paylen;
        uint32_t hdr = 2;
        uint64_t total;
        uint8_t *pl;
        uint64_t i;
        if (s_rt.rx_len < 2)
            return false;
        fin = (r[0] & 0x80) != 0;
        op = (uint8_t)(r[0] & 0x0F);
        masked = (r[1] & 0x80) != 0;
        paylen = (uint64_t)(r[1] & 0x7F);
        if (paylen == 126) {
            if (s_rt.rx_len < 4)
                return false;
            paylen = ((uint64_t)r[2] << 8) | (uint64_t)r[3];
            hdr = 4;
        } else if (paylen == 127) {
            int k;
            if (s_rt.rx_len < 10)
                return false;
            paylen = 0;
            for (k = 2; k < 10; k++)
                paylen = (paylen << 8) | (uint64_t)r[k];
            hdr = 10;
        }
        if (masked)
            hdr += 4;
        total = (uint64_t)hdr + paylen;
        if (!fin || paylen > RT_MAX_FRAME) {
            if (total > RT_RX_SIZE) {
                /* Never completable: drop it all, keep going. */
                s_rt.rx_len = 0;
                return false;
            }
            if ((uint64_t)s_rt.rx_len < total)
                return false; /* wait for the rest, then drop */
            rt_consume((uint32_t)total);
            continue;
        }
        if ((uint64_t)s_rt.rx_len < total)
            return false; /* split across callbacks: wait */
        pl = r + hdr;
        if (masked) {
            uint8_t *mk = r + hdr - 4;
            for (i = 0; i < paylen; i++)
                pl[i] ^= mk[i % 4];
        }
        switch (op) {
            case 0x8: /* close */
                MII_DEBUG_PRINTF("realtime: close frame\n");
                rt_backoff();
                return true;
            case 0x9: /* ping: answer with a pong of the same payload */
                if (rt_ws_send(0xA, pl, (uint32_t)paylen) != ERR_OK) {
                    MII_DEBUG_PRINTF("realtime: pong send failed\n");
                    rt_backoff();
                    return true;
                }
                break;
            case 0x1: /* text */
                rt_on_text(pl, (uint32_t)paylen, now);
                if (s_rt.pcb == NULL)
                    return true;
                break;
            default: /* 0x2 binary, 0xA pong, others: ignore */
                break;
        }
        rt_consume((uint32_t)total);
    }
}

static err_t
rt_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    uint32_t room;
    uint32_t n;
    (void)arg;
    (void)err;
    if (p == NULL) {
        MII_DEBUG_PRINTF("realtime: peer closed\n");
        rt_backoff();
        return ERR_OK;
    }
    room = (s_rt.rx_len < RT_RX_SIZE) ? (RT_RX_SIZE - s_rt.rx_len) : 0;
    n = p->tot_len;
    if (n > room)
        n = room;
    if (n > 0) {
        pbuf_copy_partial(p, s_rt.rx + s_rt.rx_len, (u16_t)n, 0);
        s_rt.rx_len += n;
    }
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    if (rt_parse(time_us_32()))
        return ERR_ABRT;
    return ERR_OK;
}

static void
rt_err_cb(void *arg, err_t err)
{
    (void)arg;
    /* The pcb is already freed by lwIP; just drop the pointer. */
    s_rt.pcb = NULL;
    if (s_rt.state == RT_DOWN || s_rt.state == RT_BACKOFF)
        return;
    MII_DEBUG_PRINTF("realtime: connection error %d\n", (int)err);
    s_rt.rx_len = 0;
    s_rt.backoff_until = time_us_32() + RT_BACKOFF_US;
    rt_set_state(RT_BACKOFF);
}

static err_t
rt_connected_cb(void *arg, struct altcp_pcb *pcb, err_t err)
{
    uint8_t rnd[16];
    char key[32];
    size_t olen = 0;
    int i;
    int len;
    (void)arg;
    if (err != ERR_OK) {
        MII_DEBUG_PRINTF("realtime: connect failed %d\n", (int)err);
        rt_backoff();
        return ERR_ABRT;
    }
    for (i = 0; i < 4; i++) {
        uint32_t r = get_rand_32();
        memcpy(rnd + i * 4, &r, 4);
    }
    if (mbedtls_base64_encode((unsigned char *)key, sizeof(key),
            &olen, rnd, sizeof(rnd)) != 0) {
        MII_DEBUG_PRINTF("realtime: key encode failed\n");
        rt_backoff();
        return ERR_ABRT;
    }
    len = snprintf(s_rt.req, sizeof(s_rt.req),
        "GET /realtime/v1/websocket?apikey=%s&vsn=1.0.0 HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        NETCARD_API_KEY, NETCARD_RT_HOST, key);
    if (len < 0 || len >= (int)sizeof(s_rt.req)) {
        MII_DEBUG_PRINTF("realtime: request too long\n");
        rt_backoff();
        return ERR_ABRT;
    }
    if (altcp_write(pcb, s_rt.req, (u16_t)len,
            TCP_WRITE_FLAG_COPY) != ERR_OK) {
        MII_DEBUG_PRINTF("realtime: request write failed\n");
        rt_backoff();
        return ERR_ABRT;
    }
    altcp_output(pcb);
    rt_set_state(RT_UPGRADING);
    return ERR_OK;
}

static void
rt_connect(const ip_addr_t *ip)
{
    struct altcp_tls_config *cfg = netcard_tls_config();
    if (cfg == NULL) {
        MII_DEBUG_PRINTF("realtime: tls config failed\n");
        rt_backoff();
        return;
    }
    s_rt.pcb = altcp_tls_new(cfg, IPADDR_TYPE_ANY);
    if (s_rt.pcb == NULL) {
        MII_DEBUG_PRINTF("realtime: pcb alloc failed\n");
        rt_backoff();
        return;
    }
    {
        void *tls = altcp_tls_context(s_rt.pcb);
        if (tls != NULL)
            (void)mbedtls_ssl_set_hostname(
                (mbedtls_ssl_context *)tls, NETCARD_RT_HOST);
    }
    s_rt.rx_len = 0;
    altcp_arg(s_rt.pcb, NULL);
    altcp_recv(s_rt.pcb, rt_recv_cb);
    altcp_err(s_rt.pcb, rt_err_cb);
    if (altcp_connect(s_rt.pcb, ip, RT_PORT, rt_connected_cb) != ERR_OK) {
        MII_DEBUG_PRINTF("realtime: connect start failed\n");
        rt_backoff();
        return;
    }
    rt_set_state(RT_CONNECTING);
}

static void
rt_dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    (void)arg;
    if (s_rt.state != RT_RESOLVING)
        return;
    if (ipaddr == NULL) {
        MII_DEBUG_PRINTF("realtime: dns failed\n");
        rt_backoff();
        return;
    }
    rt_connect(ipaddr);
}

static void
rt_resolve(void)
{
    ip_addr_t ip;
    err_t e;
    rt_set_state(RT_RESOLVING);
    e = dns_gethostbyname(NETCARD_RT_HOST, &ip, rt_dns_cb, NULL);
    if (e == ERR_OK) {
        /* Numeric host or cached address: connect at once. */
        rt_connect(&ip);
    } else if (e != ERR_INPROGRESS) {
        MII_DEBUG_PRINTF("realtime: dns lookup failed %d\n", (int)e);
        rt_backoff();
    }
    /* ERR_INPROGRESS: rt_dns_cb continues. */
}

void
netcard_realtime_poll(void)
{
    uint32_t now;
    if (NETCARD_RT_HOST[0] == '\0')
        return; /* idle without secrets */
    now = time_us_32();
    /* Link dropped mid-session: back off at once instead of waiting
     * for the 30 s heartbeat timeout. DOWN and BACKOFF already wait. */
    if (!netcard_link_up() &&
            s_rt.state != RT_DOWN && s_rt.state != RT_BACKOFF) {
        MII_DEBUG_PRINTF("realtime: link down\n");
        rt_backoff();
        return;
    }
    switch (s_rt.state) {
        case RT_DOWN:
            if (netcard_link_up())
                rt_resolve();
            break;
        case RT_JOINED:
            if ((uint32_t)(now - s_rt.last_hb_send) >= RT_HB_INTERVAL_US) {
                s_rt.last_hb_send = now;
                if (rt_send_heartbeat() != ERR_OK) {
                    MII_DEBUG_PRINTF("realtime: heartbeat send failed\n");
                    rt_backoff();
                    break;
                }
            }
            if ((uint32_t)(now - s_rt.last_hb_reply) >= RT_HB_TIMEOUT_US) {
                MII_DEBUG_PRINTF("realtime: heartbeat timeout\n");
                rt_backoff();
            }
            break;
        case RT_BACKOFF:
            if ((int32_t)(now - s_rt.backoff_until) >= 0)
                rt_resolve();
            break;
        default:
            break; /* RESOLVING..JOINING are event-driven */
    }
}

int
netcard_realtime_state(void)
{
    if (NETCARD_RT_HOST[0] == '\0')
        return 0;
    switch (s_rt.state) {
        case RT_JOINED:
            return 2;
        case RT_DOWN:
        case RT_BACKOFF:
            return 0;
        default:
            return 1;
    }
}
