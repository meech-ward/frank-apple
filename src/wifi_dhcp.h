/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define WIFI_DHCP_LEASES 4
#define WIFI_DHCP_PACKET_MAX 576
#define WIFI_DHCP_LEASE_SECONDS 3600
#define WIFI_DHCP_FIRST_HOST 2

/* A bounded, direct-client DHCP server for the badge's private /24 hotspot.
 * Network byte order is represented as bytes, with no alignment assumptions.
 * The only clock needed is monotonic seconds; no wall clock or timer callback. */
typedef struct {
    uint32_t expires;
    uint8_t state;
    uint8_t mac[6];
    uint8_t id_length;
    uint8_t id[255];
} wifi_dhcp_lease;

typedef struct { wifi_dhcp_lease leases[WIFI_DHCP_LEASES]; } wifi_dhcp_server;

void wifi_dhcp_init(wifi_dhcp_server *server);

/* Returns the reply length, or zero for ignored/malformed requests. reply must
 * have WIFI_DHCP_PACKET_MAX bytes; request and reply must not overlap.
 * destination is an IPv4 address: broadcast for acquisition/NAK, ciaddr for a
 * renewal or INFORM. This server never advertises Internet routing or DNS. */
size_t wifi_dhcp_reply(wifi_dhcp_server *server, const uint8_t *request,
        size_t length, uint32_t now_seconds, uint8_t *reply, size_t capacity,
        uint8_t destination[4]);
