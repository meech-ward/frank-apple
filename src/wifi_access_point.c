/* SPDX-License-Identifier: MIT */
#include "wifi_access_point.h"
#include "wifi_dhcp.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include <string.h>

static struct udp_pcb *dhcp_pcb;
static wifi_dhcp_server dhcp;
/* Keep packets off the RP2350's small stack. Raw lwIP callbacks are serialized
 * by the existing single-core poll loop, and this callback never polls again. */
static uint8_t request[WIFI_DHCP_PACKET_MAX], reply[WIFI_DHCP_PACKET_MAX];
static bool active;

static void dhcp_received(void *arg, struct udp_pcb *pcb, struct pbuf *packet,
        const ip_addr_t *sender, u16_t port) {
    (void)arg; (void)sender;
    if (!packet) return;
    size_t length = packet->tot_len;
    if (!active || port != 68 || length > sizeof(request) ||
            ip_current_input_netif() != &cyw43_state.netif[CYW43_ITF_AP] ||
            pbuf_copy_partial(packet, request, (u16_t)length, 0) != length) {
        pbuf_free(packet); return;
    }
    pbuf_free(packet);
    uint8_t destination[4];
    size_t count = wifi_dhcp_reply(&dhcp, request, length,
            (uint32_t)(time_us_64() / 1000000u), reply, sizeof(reply), destination);
    if (!count) return;
    struct pbuf *response = pbuf_alloc(PBUF_TRANSPORT, (u16_t)count, PBUF_RAM);
    if (!response) return;
    if (pbuf_take(response, reply, (u16_t)count) == ERR_OK) {
        ip_addr_t target;
        IP_ADDR4(&target, destination[0], destination[1], destination[2], destination[3]);
        udp_sendto_if(pcb, response, &target, 68, &cyw43_state.netif[CYW43_ITF_AP]);
    }
    pbuf_free(response);
}

bool wifi_access_point_start(const wifi_config *config) {
    if (active) return true;
    if (!config || config->mode != WIFI_MODE_HOTSPOT || !config->ssid[0] ||
            strlen(config->ssid) > 32 || strlen(config->password) < 8 ||
            strlen(config->password) > 63) return false;
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return false;
    ip_set_option(pcb, SOF_BROADCAST);
    if (udp_bind(pcb, IP_ANY_TYPE, 67) != ERR_OK) { udp_remove(pcb); return false; }
    cyw43_wifi_ap_set_channel(&cyw43_state, 6);
    cyw43_arch_enable_ap_mode(config->ssid, config->password, CYW43_AUTH_WPA2_AES_PSK);
    struct netif *netif = &cyw43_state.netif[CYW43_ITF_AP];
    if (!netif_is_up(netif)) {
        udp_remove(pcb);
        cyw43_arch_disable_ap_mode();
        cyw43_cb_tcpip_deinit(&cyw43_state, CYW43_ITF_AP);
        return false;
    }
    ip4_addr_t address, mask;
    IP4_ADDR(&address, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    netif_set_addr(netif, &address, &mask, &address);
    netif_set_default(netif);
    udp_bind_netif(pcb, netif);
    wifi_dhcp_init(&dhcp);
    dhcp_pcb = pcb;
    active = true;
    udp_recv(pcb, dhcp_received, NULL);
    return true;
}

void wifi_access_point_stop(void) {
    if (dhcp_pcb) { udp_remove(dhcp_pcb); dhcp_pcb = NULL; }
    if (active) {
        cyw43_arch_disable_ap_mode();
        cyw43_cb_tcpip_deinit(&cyw43_state, CYW43_ITF_AP);
    }
    active = false;
    wifi_dhcp_init(&dhcp);
}

bool wifi_access_point_active(void) { return active; }
