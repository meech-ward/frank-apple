/*
 * mii_netcard.c - Virtual WiFi network card in slot 1.
 *
 * Register map (slot 1 = $C090 + reg, reg = addr & 0x0F):
 *   reg  read                                          write
 *   0    link: 0 = no wifi, 1 = wifi up                any value: start fetch if
 *                                                      not FETCHING (clears buffer)
 *   1    state: 0 IDLE, 1 FETCHING, 2 READY, 3 ERROR    ignored
 *   2    next body byte, or 0 when exhausted            any value: rewind read
 *        or not READY; auto-advances                   pointer to 0
 *   3    bytes remaining, low byte                     ignored
 *   4    bytes remaining, high byte                    ignored
 *   5    realtime state with NETCARD_REALTIME:           ignored
 *        0 DOWN/BACKOFF, 1 RESOLVING..JOINING, 2 JOINED
 *        (0 without NETCARD_REALTIME)
 *   6-15 0                                             ignored
 *
 * Body handling: LF (0x0A) is returned as CR (0x0D), every byte masked
 * with 0x7F. One static 4096-byte buffer; overflow is dropped.
 *
 * Fetch engine: a small hand-rolled HTTP/1.1 GET over lwIP's altcp API,
 * which covers both plain TCP and TLS (mbedTLS) behind one interface.
 * With NETCARD_TLS the pcb is a TLS client. With NETCARD_TLS_VERIFY
 * the client is created with the bundled CA roots and verification is
 * REQUIRED; without it the client is created with no CA and
 * verification is OPTIONAL (the lwIP default authmode). SNI is set
 * from NETCARD_HOST. NETCARD_HOST may be a hostname (DNS) or a
 * dotted IP. When NETCARD_API_KEY is non-empty the request carries
 * the key in apikey and Authorization header lines. A new fetch while
 * one is in flight is ignored, as before.
 *
 * Boot is async: netcard_init() starts the WiFi join and returns at
 * once; a link monitor in netcard_poll() (every 500 ms) tracks
 * CYW43_LINK_UP and retries the join while the link stays down.
 * A fetch in flight when the link drops is torn down to ERROR.
 *
 * Poll mode: every lwIP callback runs inside cyw43_arch_poll() on
 * core 0. No locking. A TLS handshake stalls the emulator for a second
 * or two; that is accepted.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mii.h"
#include "mii_slot.h"
#include "debug_log.h"
#include "netcard.h"
#if NETCARD_WEB_CONTROL
#include "web_control.h"
#endif

#include "netcard_config.h"

#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#if NETCARD_TLS
#include "lwip/altcp_tls.h"
#endif
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#if NETCARD_TLS
#include "mbedtls/ssl.h"
#endif
#ifdef NETCARD_TLS_VERIFY
#include "netcard_ca.h"
#endif

enum {
    NC_IDLE = 0,
    NC_FETCHING = 1,
    NC_READY = 2,
    NC_ERROR = 3,
};

#define NC_BODY_SIZE 4096
#define NC_RAW_SIZE  (NC_BODY_SIZE + 1024)
#define NC_REQ_SIZE  1024
/* altcp_poll interval is 10 x 500 ms = 5 s; give up after 6 quiet polls. */
#define NC_POLL_INTERVAL 10
#define NC_POLL_LIMIT    6

typedef struct {
    uint8_t link;      /* 0 = no wifi, 1 = wifi up */
    uint8_t state;     /* NC_* */
    uint8_t body[NC_BODY_SIZE];
    uint32_t len;      /* valid bytes in body */
    uint32_t pos;      /* next byte to return for reg 2 */
    bool init_ok;      /* cyw43_arch_init() succeeded */
    struct altcp_pcb *pcb;
    uint8_t raw[NC_RAW_SIZE]; /* headers plus body; overflow is dropped */
    uint32_t raw_len;  /* valid bytes in raw */
    uint8_t quiet_polls; /* polls with no progress */
} netcard_state_t;

static netcard_state_t s_nc;
static char s_req[NC_REQ_SIZE];
#if NETCARD_TLS
static struct altcp_tls_config *s_tls_cfg;
#endif

/* Link monitor: the status register is read at most every 500 ms;
 * while the link stays down a rejoin is attempted 5 s after the drop
 * and allows each association/DHCP attempt 30 s. Never blocks, never sleeps. */
#define NC_LINK_CHECK_US (500u * 1000u)
#define NC_LINK_RETRY_FIRST_US (5u * 1000u * 1000u)
#define NC_LINK_JOIN_TIMEOUT_US (30u * 1000u * 1000u)

static uint32_t s_link_check_last;
static uint32_t s_reconnect_at;

/* Detach callbacks, then free the pcb. close_ok selects close vs abort. */
static void
nc_pcb_teardown(bool close_ok)
{
    struct altcp_pcb *pcb = s_nc.pcb;
    s_nc.pcb = NULL;
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
nc_fail(void)
{
    nc_pcb_teardown(false);
    s_nc.state = NC_ERROR;
}

/* Expect "HTTP/1.x NNN"; return NNN or -1. */
static int
nc_status_code(void)
{
    if (s_nc.raw_len < 13)
        return -1;
    if (memcmp(s_nc.raw, "HTTP/1.", 7) != 0)
        return -1;
    /* "HTTP/1.x NNN": minor digit at 7, space at 8, code at 9..11. */
    if (s_nc.raw[8] != ' ')
        return -1;
    for (int i = 9; i < 12; i++) {
        if (s_nc.raw[i] < '0' || s_nc.raw[i] > '9')
            return -1;
    }
    return (s_nc.raw[9] - '0') * 100 +
           (s_nc.raw[10] - '0') * 10 +
           (s_nc.raw[11] - '0');
}

/* Offset just past the "\r\n\r\n" header end, or -1 when absent. */
static int
nc_header_end(void)
{
    uint32_t i;
    for (i = 0; i + 4 <= s_nc.raw_len; i++) {
        if (s_nc.raw[i] == '\r' && s_nc.raw[i + 1] == '\n' &&
            s_nc.raw[i + 2] == '\r' && s_nc.raw[i + 3] == '\n')
            return (int)(i + 4);
    }
    return -1;
}

/* Peer closed: parse what arrived and settle READY vs ERROR. */
static void
nc_finish(void)
{
    int code = nc_status_code();
    int hdr = nc_header_end();
    nc_pcb_teardown(true);
    s_nc.len = 0;
    s_nc.pos = 0;
    if (code == 200 && hdr >= 0) {
        uint32_t n = s_nc.raw_len - (uint32_t)hdr;
        if (n > NC_BODY_SIZE)
            n = NC_BODY_SIZE;
        memcpy(s_nc.body, s_nc.raw + hdr, n);
        s_nc.len = n;
        s_nc.state = NC_READY;
    } else {
        s_nc.state = NC_ERROR;
        MII_DEBUG_PRINTF("netcard: fetch failed status=%d\n", code);
    }
}

static err_t
nc_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    (void)err;
    if (p == NULL) {
        /* Peer closed the connection: finish. */
        nc_finish();
        return ERR_OK;
    }
    s_nc.quiet_polls = 0;
    uint32_t room = (s_nc.raw_len < NC_RAW_SIZE) ?
        (NC_RAW_SIZE - s_nc.raw_len) : 0;
    uint32_t n = p->tot_len;
    if (n > room)
        n = room;
    if (n > 0) {
        pbuf_copy_partial(p, s_nc.raw + s_nc.raw_len, (u16_t)n, 0);
        s_nc.raw_len += n;
    }
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void
nc_err_cb(void *arg, err_t err)
{
    (void)arg;
    /* The pcb is already freed by lwIP; just drop the pointer. */
    s_nc.pcb = NULL;
    if (s_nc.state == NC_FETCHING) {
        s_nc.state = NC_ERROR;
        MII_DEBUG_PRINTF("netcard: connection error %d\n", (int)err);
    }
}

static err_t
nc_poll_cb(void *arg, struct altcp_pcb *pcb)
{
    (void)arg;
    (void)pcb;
    if (s_nc.state != NC_FETCHING)
        return ERR_OK;
    if (++s_nc.quiet_polls >= NC_POLL_LIMIT) {
        MII_DEBUG_PRINTF("netcard: fetch timed out\n");
        nc_fail();
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t
nc_connected_cb(void *arg, struct altcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK) {
        MII_DEBUG_PRINTF("netcard: connect failed %d\n", (int)err);
        nc_fail();
        return ERR_ABRT;
    }
    int len;
    if (NETCARD_API_KEY[0] != '\0') {
        len = snprintf(s_req, sizeof(s_req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: frank-apple\r\n"
            "Accept: %s\r\n"
            "Connection: close\r\n"
            "apikey: %s\r\n"
            "Authorization: Bearer %s\r\n"
            "\r\n",
            NETCARD_PATH, NETCARD_HOST, NETCARD_ACCEPT,
            NETCARD_API_KEY, NETCARD_API_KEY);
    } else {
        len = snprintf(s_req, sizeof(s_req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: frank-apple\r\n"
            "Accept: %s\r\n"
            "Connection: close\r\n"
            "\r\n",
            NETCARD_PATH, NETCARD_HOST, NETCARD_ACCEPT);
    }
    if (len < 0 || len >= (int)sizeof(s_req)) {
        MII_DEBUG_PRINTF("netcard: request too long\n");
        nc_fail();
        return ERR_ABRT;
    }
    s_nc.quiet_polls = 0;
    if (altcp_write(pcb, s_req, (u16_t)len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
        MII_DEBUG_PRINTF("netcard: request write failed\n");
        nc_fail();
        return ERR_ABRT;
    }
    altcp_output(pcb);
    return ERR_OK;
}

static void
nc_connect(const ip_addr_t *ip)
{
#if NETCARD_TLS
    if (s_tls_cfg == NULL) {
#ifdef NETCARD_TLS_VERIFY
        /* Bundled CA roots: verification is REQUIRED. */
        s_tls_cfg = altcp_tls_create_config_client(
                netcard_ca_pem, NETCARD_CA_PEM_LEN);
#else
        /* No CA: verification is OPTIONAL, the lwIP default authmode. */
        s_tls_cfg = altcp_tls_create_config_client(NULL, 0);
#endif
        if (s_tls_cfg == NULL) {
            MII_DEBUG_PRINTF("netcard: tls config failed\n");
            s_nc.state = NC_ERROR;
            return;
        }
    }
    s_nc.pcb = altcp_tls_new(s_tls_cfg, IPADDR_TYPE_ANY);
#else
    s_nc.pcb = altcp_tcp_new_ip_type(IPADDR_TYPE_ANY);
#endif
    if (s_nc.pcb == NULL) {
        MII_DEBUG_PRINTF("netcard: pcb alloc failed\n");
        s_nc.state = NC_ERROR;
        return;
    }
#if NETCARD_TLS
    {
        void *tls = altcp_tls_context(s_nc.pcb);
        if (tls != NULL)
            (void)mbedtls_ssl_set_hostname(
                (mbedtls_ssl_context *)tls, NETCARD_HOST);
    }
#endif
    altcp_arg(s_nc.pcb, NULL);
    altcp_recv(s_nc.pcb, nc_recv_cb);
    altcp_err(s_nc.pcb, nc_err_cb);
    altcp_poll(s_nc.pcb, nc_poll_cb, NC_POLL_INTERVAL);
    if (altcp_connect(s_nc.pcb, ip, NETCARD_PORT,
            nc_connected_cb) != ERR_OK) {
        MII_DEBUG_PRINTF("netcard: connect start failed\n");
        nc_fail();
    }
}

static void
nc_dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    (void)arg;
    if (s_nc.state != NC_FETCHING)
        return;
    if (ipaddr == NULL) {
        MII_DEBUG_PRINTF("netcard: dns failed\n");
        s_nc.state = NC_ERROR;
        return;
    }
    nc_connect(ipaddr);
}

static void
nc_start_fetch(void)
{
    if (s_nc.state == NC_FETCHING)
        return;
    if (!s_nc.link) {
        s_nc.state = NC_ERROR;
        return;
    }
    s_nc.len = 0;
    s_nc.pos = 0;
    s_nc.raw_len = 0;
    s_nc.quiet_polls = 0;
    s_nc.pcb = NULL;
    s_nc.state = NC_FETCHING;
    ip_addr_t ip;
    err_t e = dns_gethostbyname(NETCARD_HOST, &ip, nc_dns_cb, NULL);
    if (e == ERR_OK) {
        /* Numeric host or cached address: connect at once. */
        nc_connect(&ip);
    } else if (e != ERR_INPROGRESS) {
        s_nc.state = NC_ERROR;
        MII_DEBUG_PRINTF("netcard: dns lookup failed %d\n", (int)e);
    }
    /* ERR_INPROGRESS: nc_dns_cb continues the fetch. */
}

/* Link monitor, called from netcard_poll(). Reads the link status at
 * most every 500 ms. CYW43_LINK_UP sets link = 1, anything else sets
 * link = 0; each transition logs once. While the link stays down the
 * join is retried 5 s after the drop, then every 30 s. A fetch in
 * flight when the link drops is torn down to ERROR. */
static void
nc_link_poll(uint32_t now)
{
    int st;
    if ((uint32_t)(now - s_link_check_last) < NC_LINK_CHECK_US)
        return;
    s_link_check_last = now;
    st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    static int last_status = 99;
    if (st != last_status) {
        MII_DEBUG_PRINTF("netcard: link status=%d\n", st);
        last_status = st;
    }
    if (st == CYW43_LINK_UP) {
        if (!s_nc.link) {
            s_nc.link = 1;
            MII_DEBUG_PRINTF("netcard: wifi up, ip %s\n",
                    ip4addr_ntoa(netif_ip4_addr(netif_default)));
            int32_t rssi;
            if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) == 0)
                MII_DEBUG_PRINTF("netcard: signal %ld dBm\n", (long)rssi);
        }
        return;
    }
    if (s_nc.link) {
        s_nc.link = 0;
        MII_DEBUG_PRINTF("netcard: wifi down, status=%d\n", st);
        s_reconnect_at = now + NC_LINK_RETRY_FIRST_US;
        if (s_nc.state == NC_FETCHING) {
            MII_DEBUG_PRINTF("netcard: fetch aborted, link down\n");
            nc_fail();
        }
    } else if ((int32_t)(now - s_reconnect_at) >= 0) {
        s_reconnect_at = now + NC_LINK_JOIN_TIMEOUT_US;
        MII_DEBUG_PRINTF("netcard: wifi retry status=%d\n", st);
        cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASS,
                CYW43_AUTH_WPA2_AES_PSK);
    }
}

void
netcard_init(void)
{
    memset(&s_nc, 0, sizeof(s_nc));
    s_nc.state = NC_IDLE;
    /* Link starts down; the monitor below tracks the async join. */
    s_nc.link = 0;
    s_link_check_last = 0;
    s_reconnect_at = time_us_32() + NC_LINK_JOIN_TIMEOUT_US;
    int r = cyw43_arch_init();
    if (r != 0) {
        MII_DEBUG_PRINTF("netcard: cyw43 init failed %d\n", r);
        s_nc.init_ok = false;
        return;
    }
    MII_DEBUG_PRINTF("netcard: radio initialized\n");
    s_nc.init_ok = true;
    cyw43_arch_enable_sta_mode();
    /* Async join: the emulator starts at once, no 15 s block. */
    r = cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASS,
            CYW43_AUTH_WPA2_AES_PSK);
    MII_DEBUG_PRINTF("netcard: join requested rc=%d\n", r);
}

#if NETCARD_REALTIME
struct altcp_tls_config *
netcard_tls_config(void)
{
    if (s_tls_cfg == NULL) {
#ifdef NETCARD_TLS_VERIFY
        s_tls_cfg = altcp_tls_create_config_client(
                netcard_ca_pem, NETCARD_CA_PEM_LEN);
#else
        s_tls_cfg = altcp_tls_create_config_client(NULL, 0);
#endif
    }
    return s_tls_cfg;
}

bool
netcard_link_up(void)
{
    return s_nc.link != 0;
}
#endif

void
netcard_poll(void)
{
    if (!s_nc.init_ok)
        return;
    cyw43_arch_poll();
    nc_link_poll(time_us_32());
#if NETCARD_REALTIME
    netcard_realtime_poll();
#endif
#if NETCARD_WEB_CONTROL
    web_control_poll();
#endif
}

static uint8_t
_netcard_access(mii_t *mii, struct mii_slot_t *slot,
        uint16_t addr, uint8_t byte, bool write)
{
    (void)mii;
    (void)slot;
    uint8_t reg = (uint8_t)(addr & 0x0F);
    if (write) {
        if (reg == 0)
            nc_start_fetch();
        else if (reg == 2)
            s_nc.pos = 0;
        return 0;
    }
    switch (reg) {
        case 0:
            return s_nc.link;
        case 1:
            return s_nc.state;
        case 2: {
            if (s_nc.state != NC_READY)
                return 0;
            if (s_nc.pos >= s_nc.len)
                return 0;
            uint8_t b = s_nc.body[s_nc.pos++];
            if (b == 0x0A)
                b = 0x0D;
            return (uint8_t)(b & 0x7F);
        }
        case 3: {
            uint32_t rem = 0;
            if (s_nc.state == NC_READY && s_nc.len > s_nc.pos)
                rem = s_nc.len - s_nc.pos;
            return (uint8_t)(rem & 0xFF);
        }
        case 4: {
            uint32_t rem = 0;
            if (s_nc.state == NC_READY && s_nc.len > s_nc.pos)
                rem = s_nc.len - s_nc.pos;
            return (uint8_t)((rem >> 8) & 0xFF);
        }
        case 5:
#if NETCARD_REALTIME
            return (uint8_t)netcard_realtime_state();
#else
            return 0;
#endif
        default:
            return 0;
    }
}

static int
_netcard_init(mii_t *mii, struct mii_slot_t *slot)
{
    (void)mii;
    slot->drv_priv = &s_nc;
    return 0;
}

static mii_slot_drv_t _driver = {
    .name = "netcard",
    .desc = "Virtual WiFi network card",
    .init = _netcard_init,
    .access = _netcard_access,
};
MI_DRIVER_REGISTER(_driver);
