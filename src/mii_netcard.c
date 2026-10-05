/* Virtual slot-1 WiFi card. Original registers and URL GET API stay compatible.
 * Extension revision 2 (read reg 15): reg 13 selects GET/POST/PUT/PATCH/DELETE,
 * reg 14 appends header bytes, reg 15 appends body bytes. Reset clears all input.
 * Wi-Fi comes only from /wifi.ini. No built-in cloud account or credentials.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mii.h"
#include "mii_slot.h"
#include "debug_log.h"
#include "netcard.h"
#include "net_http.h"
#if NETCARD_WEB_CONTROL
#include "web_control.h"
#endif
#include "wifi_config.h"
#include "wifi_access_point.h"
#if NETCARD_SSH
#include "ssh_control.h"
#endif
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#if NETCARD_TLS
#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#endif
#ifdef NETCARD_TLS_VERIFY
#include "netcard_ca.h"
#endif

enum { NC_IDLE, NC_FETCHING, NC_READY, NC_ERROR };
#define NC_REQ_SIZE NH_REQUEST_MAX
#define NC_TIMEOUT_US (30u * 1000u * 1000u)
#define NC_LINK_CHECK_US (500u * 1000u)
#define NC_LINK_RETRY_FIRST_US (5u * 1000u * 1000u)
#define NC_LINK_JOIN_TIMEOUT_US (30u * 1000u * 1000u)

typedef struct {
    uint8_t link, state, error, redirects;
    bool init_ok, legacy, follow, url_overflow, headers_overflow, body_overflow;
    uint8_t method;
    uint16_t url_len, headers_len, body_len;
    uint32_t pos, deadline;
    uintptr_t generation;
    struct altcp_pcb *pcb;
    char url_text[NH_URL_MAX + 1];
    char headers[NH_REQUEST_HEADERS_MAX + 1];
    uint8_t body[NH_REQUEST_BODY_MAX];
    nh_url url;
    nh_response response;
} netcard_state_t;
static netcard_state_t s_nc;
static char s_req[NC_REQ_SIZE];
static wifi_config s_wifi;
static enum wifi_config_status s_wifi_status;
static int s_link_status;
static uint32_t s_link_check_last, s_reconnect_at;
#if NETCARD_TLS
static struct altcp_tls_config *s_tls_cfg;
static struct altcp_tls_config *nc_tls_config(void) {
    if (!s_tls_cfg) {
#ifdef NETCARD_TLS_VERIFY
        s_tls_cfg = altcp_tls_create_config_client(netcard_ca_pem, NETCARD_CA_PEM_LEN);
#else
        s_tls_cfg = altcp_tls_create_config_client(NULL, 0);
#endif
    }
    return s_tls_cfg;
}
#endif

/* Return true if a callback must return ERR_ABRT. */
static bool nc_pcb_teardown(bool close_ok) {
    struct altcp_pcb *pcb = s_nc.pcb;
    s_nc.pcb = NULL;
    if (!pcb) return false;
    altcp_arg(pcb, NULL); altcp_recv(pcb, NULL); altcp_err(pcb, NULL); altcp_poll(pcb, NULL, 0);
    if (close_ok && altcp_close(pcb) == ERR_OK) return false;
    altcp_abort(pcb); return true;
}
static void nc_fail(uint8_t error) {
    nc_pcb_teardown(false);
    ++s_nc.generation; /* invalidate any outstanding DNS callback */
    s_nc.follow = false; s_nc.error = error; s_nc.state = NC_ERROR; s_nc.pos = 0;
    MII_DEBUG_PRINTF("netcard: HTTP error=%u http=%u\n", error, s_nc.response.status);
}
static bool nc_current(void *arg) { return (uintptr_t)arg == s_nc.generation && s_nc.state == NC_FETCHING; }

static err_t nc_complete(void) {
    bool aborted = nc_pcb_teardown(true);
    if (s_nc.response.redirect_headers_only && nh_is_redirect(s_nc.response.status) && s_nc.response.location[0]) {
        if (s_nc.redirects == 3) nc_fail(NH_REDIRECT);
        else s_nc.follow = true; /* resolve/connect outside the lwIP callback */
    } else if (s_nc.legacy && s_nc.response.status != 200) nc_fail(NH_HTTP_STATUS);
    else {
        s_nc.pos = 0; s_nc.error = NH_OK; s_nc.state = NC_READY;
        MII_DEBUG_PRINTF("netcard: HTTP ready http=%u bytes=%u\n", s_nc.response.status, (unsigned)s_nc.response.size);
    }
    return aborted ? ERR_ABRT : ERR_OK;
}
static err_t nc_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (!nc_current(arg) || pcb != s_nc.pcb) { if (p) pbuf_free(p); return ERR_OK; }
    if (err != ERR_OK) { if (p) pbuf_free(p); nc_fail(NH_CONNECT); return ERR_ABRT; }
    if (!p) nh_eof(&s_nc.response);
    else {
        for (struct pbuf *q = p; q; q = q->next) nh_feed(&s_nc.response, q->payload, q->len);
        altcp_recved(pcb, p->tot_len); pbuf_free(p);
    }
    if (s_nc.response.phase == NH_FAILED) { nc_fail(s_nc.response.error); return ERR_ABRT; }
    if (s_nc.response.phase == NH_DONE) return nc_complete();
    return ERR_OK;
}
static void nc_err_cb(void *arg, err_t err) {
    (void)err;
    if (!nc_current(arg)) return;
    s_nc.pcb = NULL; /* lwIP already freed it */
    nc_fail(s_nc.url.tls ? NH_TLS : NH_CONNECT);
}
static err_t nc_connected_cb(void *arg, struct altcp_pcb *pcb, err_t err) {
    if (!nc_current(arg) || pcb != s_nc.pcb) return ERR_OK;
    if (err != ERR_OK) { nc_fail(s_nc.url.tls ? NH_TLS : NH_CONNECT); return ERR_ABRT; }
    MII_DEBUG_PRINTF("netcard: HTTP connected (%s), sending request\n", s_nc.url.tls ? "TLS" : "TCP");
    int len = nh_request(s_req, sizeof(s_req), &s_nc.url,
        s_nc.legacy ? NH_GET : s_nc.method, s_nc.legacy ? "" : s_nc.headers,
        s_nc.body, s_nc.legacy ? 0 : s_nc.body_len);
    if (len < 0) { nc_fail((uint8_t)-len); return ERR_ABRT; }
    if (altcp_write(pcb, s_req, (u16_t)len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
        nc_fail(NH_CONNECT); return ERR_ABRT;
    }
    altcp_output(pcb); return ERR_OK;
}
static void nc_connect(const ip_addr_t *ip) {
    MII_DEBUG_PRINTF("netcard: HTTP connecting (%s)\n", s_nc.url.tls ? "TLS" : "TCP");
    if (s_nc.url.tls) {
#if NETCARD_TLS && NETCARD_TLS_VERIFY
        struct altcp_tls_config *cfg = nc_tls_config();
        if (!cfg) { nc_fail(NH_TLS); return; }
        s_nc.pcb = altcp_tls_new(cfg, IPADDR_TYPE_V4);
#else
        nc_fail(NH_UNSUPPORTED); return;
#endif
    } else s_nc.pcb = altcp_tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!s_nc.pcb) { nc_fail(NH_CONNECT); return; }
#if NETCARD_TLS
    if (s_nc.url.tls) {
        mbedtls_ssl_context *tls = altcp_tls_context(s_nc.pcb);
        if (!tls || mbedtls_ssl_set_hostname(tls, s_nc.url.host)) { nc_fail(NH_TLS); return; }
    }
#endif
    altcp_arg(s_nc.pcb, (void *)s_nc.generation);
    altcp_recv(s_nc.pcb, nc_recv_cb); altcp_err(s_nc.pcb, nc_err_cb);
    /* Install the TLS layer's lower poll callback even without an app callback:
     * it retries buffered output/receive data after transient packet shortages. */
    altcp_poll(s_nc.pcb, NULL, 2);
    if (altcp_connect(s_nc.pcb, ip, s_nc.url.port, nc_connected_cb) != ERR_OK) nc_fail(NH_CONNECT);
}
static void nc_dns_cb(const char *name, const ip_addr_t *ip, void *arg) {
    (void)name;
    if (!nc_current(arg)) return;
    if (!ip) nc_fail(NH_DNS); else nc_connect(ip);
}
static void nc_resolve(void) {
    ++s_nc.generation;
    MII_DEBUG_PRINTF("netcard: HTTP resolving, redirect=%u\n", s_nc.redirects);
    ip_addr_t ip;
    err_t e = dns_gethostbyname_addrtype(s_nc.url.host, &ip, nc_dns_cb,
        (void *)s_nc.generation, LWIP_DNS_ADDRTYPE_IPV4);
    if (e == ERR_OK) nc_connect(&ip);
    else if (e != ERR_INPROGRESS) nc_fail(NH_DNS);
}
static void nc_start_fetch(bool legacy) {
    if (s_nc.state == NC_FETCHING) return;
    nh_init(&s_nc.response); s_nc.pos = 0; s_nc.error = NH_OK;
    s_nc.legacy = legacy; s_nc.redirects = 0; s_nc.follow = false;
    if (!s_nc.link) { nc_fail(NH_WIFI); return; }
    int e;
    if (legacy) e = nh_parse_url("https://httpbin.org/uuid", &s_nc.url);
    else if (s_nc.url_overflow) e = NH_URL_LONG;
    else if (s_nc.headers_overflow || s_nc.body_overflow) e = NH_TOO_LARGE;
    else e = nh_parse_url(s_nc.url_text, &s_nc.url);
    if (e) { nc_fail(e); return; }
    /* Validate all staged bytes before opening a socket. Only plain GETs are
     * automatically redirected. Writes and authenticated/custom-header GETs
     * return 3xx as-is, avoiding accidental replay or credential forwarding. */
    int len = nh_request(s_req, sizeof(s_req), &s_nc.url,
        legacy ? NH_GET : s_nc.method, legacy ? "" : s_nc.headers,
        s_nc.body, legacy ? 0 : s_nc.body_len);
    if (len < 0) { nc_fail((uint8_t)-len); return; }
    s_nc.response.redirect_headers_only = legacy || (s_nc.method == NH_GET && !s_nc.headers_len);
    s_nc.state = NC_FETCHING; s_nc.deadline = time_us_32() + NC_TIMEOUT_US;
    nc_resolve();
}
static void nc_command(uint8_t cmd) {
    if (cmd == 0) {
        nc_pcb_teardown(false); ++s_nc.generation;
        s_nc.follow = false; s_nc.state = NC_IDLE; s_nc.error = NH_OK;
        s_nc.url_len = 0; s_nc.url_text[0] = 0; s_nc.url_overflow = false; s_nc.pos = 0;
        s_nc.method = NH_GET; s_nc.headers_len = s_nc.body_len = 0;
        s_nc.headers_overflow = s_nc.body_overflow = false;
        memset(s_nc.headers, 0, sizeof(s_nc.headers));
        memset(s_nc.body, 0, sizeof(s_nc.body));
        memset(s_req, 0, sizeof(s_req));
        nh_init(&s_nc.response);
    } else if (cmd == 1) nc_start_fetch(false);
    else if (cmd == 2 && s_nc.state == NC_FETCHING) nc_fail(NH_CANCELLED);
}

void netcard_cancel(void) {
    if (s_nc.state == NC_FETCHING) nc_fail(NH_CANCELLED);
}

/* Link monitor, called from netcard_poll(). Reads the link status at
 * most every 500 ms. CYW43_LINK_UP sets link = 1, anything else sets
 * link = 0; each transition logs once. While the link stays down the
 * join is retried 5 s after the drop, then every 30 s. A fetch in
 * flight when the link drops is torn down to ERROR. */
static void
nc_link_poll(uint32_t now)
{
    if (s_wifi.mode == WIFI_MODE_HOTSPOT) {
        s_nc.link = wifi_access_point_active() ? 1 : 0;
        return; /* CYW43's STA link status does not describe its AP interface. */
    }
    int st;
    if ((uint32_t)(now - s_link_check_last) < NC_LINK_CHECK_US)
        return;
    s_link_check_last = now;
    st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    s_link_status = st;
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
            nc_fail(NH_WIFI);
        }
    } else if ((int32_t)(now - s_reconnect_at) >= 0) {
        s_reconnect_at = now + NC_LINK_JOIN_TIMEOUT_US;
        MII_DEBUG_PRINTF("netcard: wifi retry status=%d\n", st);
        cyw43_arch_wifi_connect_async(s_wifi.ssid, s_wifi.password,
                s_wifi.password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
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
    s_wifi_status = wifi_config_load(&s_wifi);
    if (s_wifi_status != WIFI_CONFIG_READY) {
        MII_DEBUG_PRINTF("netcard: WiFi disabled, wifi.ini status=%d\n", s_wifi_status);
        return;
    }
    int r = cyw43_arch_init();
    if (r != 0) {
        MII_DEBUG_PRINTF("netcard: cyw43 init failed %d\n", r);
        s_nc.init_ok = false;
        return;
    }
    MII_DEBUG_PRINTF("netcard: radio initialized\n");
    s_nc.init_ok = true;
    if (s_wifi.mode == WIFI_MODE_HOTSPOT) {
        s_nc.link = wifi_access_point_start(&s_wifi) ? 1 : 0;
        MII_DEBUG_PRINTF("netcard: hotspot %s at 192.168.4.1\n", s_nc.link ? "ready" : "failed");
    } else {
        cyw43_arch_enable_sta_mode();
        /* Async join: the emulator starts at once, no 15 s block. */
        r = cyw43_arch_wifi_connect_async(s_wifi.ssid, s_wifi.password,
                s_wifi.password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
        MII_DEBUG_PRINTF("netcard: join requested rc=%d\n", r);
    }
#if NETCARD_SSH
    ssh_control_init(&s_wifi);
#endif
}

const char *netcard_wifi_status(void) {
    if (s_wifi_status == WIFI_CONFIG_MISSING) return "WiFi off: add /wifi.ini";
    if (s_wifi_status == WIFI_CONFIG_INVALID) return "WiFi off: invalid /wifi.ini";
    if (s_wifi_status == WIFI_CONFIG_IO) return "WiFi off: cannot read /wifi.ini";
    if (!s_nc.init_ok) return "WiFi radio unavailable";
    if (s_wifi.mode == WIFI_MODE_HOTSPOT && !s_nc.link) return "WiFi hotspot could not start";
    if (s_nc.link) return NULL;
    if (s_link_status == CYW43_LINK_BADAUTH) return "WiFi password rejected; retrying";
    if (s_link_status == CYW43_LINK_NONET) return "WiFi network not found; retrying";
    return "Connecting to WiFi...";
}

void
netcard_poll(void)
{
    if (!s_nc.init_ok)
        return;
    cyw43_arch_poll();
    uint32_t now = time_us_32();
    nc_link_poll(now);
    if (s_nc.state == NC_FETCHING && (int32_t)(now - s_nc.deadline) >= 0) nc_fail(NH_TIMEOUT);
    if (s_nc.state == NC_FETCHING && s_nc.follow) {
        /* URL resolution supports in-place output; keep large URL structs off
         * the RP2350's small stack. This runs outside TLS/lwIP callbacks. */
        int e = nh_resolve(&s_nc.url, s_nc.response.location, &s_nc.url);
        if (e) nc_fail(e);
        else {
            ++s_nc.redirects; s_nc.follow = false;
            nh_init(&s_nc.response); nc_resolve();
        }
    }
#if NETCARD_WEB_CONTROL
    web_control_poll();
#endif
#if NETCARD_SSH
    ssh_control_poll(s_nc.link != 0);
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
            nc_start_fetch(true);
        else if (reg == 2)
            s_nc.pos = 0;
        else if (reg == 6) nc_command(byte);
        else if (reg == 7 && s_nc.state != NC_FETCHING) {
            if (!byte || s_nc.url_len == NH_URL_MAX) s_nc.url_overflow = true;
            else {
                s_nc.url_text[s_nc.url_len++] = (char)byte;
                s_nc.url_text[s_nc.url_len] = 0;
            }
        } else if (reg == 13 && s_nc.state != NC_FETCHING) s_nc.method = byte;
        else if (reg == 14 && s_nc.state != NC_FETCHING) {
            if (!byte || s_nc.headers_len == NH_REQUEST_HEADERS_MAX) s_nc.headers_overflow = true;
            else {
                s_nc.headers[s_nc.headers_len++] = (char)byte;
                s_nc.headers[s_nc.headers_len] = 0;
            }
        } else if (reg == 15 && s_nc.state != NC_FETCHING) {
            if (s_nc.body_len == NH_REQUEST_BODY_MAX) s_nc.body_overflow = true;
            else s_nc.body[s_nc.body_len++] = byte;
        }
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
            if (s_nc.pos >= s_nc.response.size)
                return 0;
            uint8_t b = s_nc.response.body[s_nc.pos++];
            if (b == 0x0A)
                b = 0x0D;
            return (uint8_t)(b & 0x7F);
        }
        case 3: {
            uint32_t rem = 0;
            if (s_nc.state == NC_READY && s_nc.response.size > s_nc.pos)
                rem = s_nc.response.size - s_nc.pos;
            return (uint8_t)(rem & 0xFF);
        }
        case 4: {
            uint32_t rem = 0;
            if (s_nc.state == NC_READY && s_nc.response.size > s_nc.pos)
                rem = s_nc.response.size - s_nc.pos;
            return (uint8_t)((rem >> 8) & 0xFF);
        }
        case 5: return 0; /* Former cloud-keyboard status, retained for old programs. */
        case 6: return 1;
        case 7:
            return s_nc.state == NC_READY && s_nc.pos < s_nc.response.size ? s_nc.response.body[s_nc.pos++] : 0;
        case 8: return s_nc.response.status & 0xff;
        case 9: return s_nc.response.status >> 8;
        case 10: return s_nc.error;
        case 11: return s_nc.url_len & 0xff;
        case 12: return s_nc.url_len >> 8;
        case 13: return s_nc.method;
        case 14: return 0;
        case 15: return 2; /* HTTP extension revision; base API still reports 1. */
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
