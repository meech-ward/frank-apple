/* SPDX-License-Identifier: MIT
 * Original implementation of the direct-client subset of RFC 2131/2132 and
 * RFC 6842. No SDK/example DHCP implementation is copied here. */
#include "wifi_dhcp.h"
#include <stdbool.h>
#include <string.h>

enum { FREE, OFFERED, BOUND, DECLINED };
enum { DISCOVER=1, OFFER, REQUEST, DECLINE, ACK, NAK, RELEASE, INFORM };
static const uint8_t address[4] = {192, 168, 4, 1};
static const uint8_t mask[4] = {255, 255, 255, 0};
static const uint8_t cookie[4] = {99, 130, 83, 99};

typedef struct {
    uint8_t type;
    const uint8_t *requested, *server, *id;
    uint8_t id_length;
} dhcp_options;

static bool zero_ip(const uint8_t *ip) {
    return !(ip[0] | ip[1] | ip[2] | ip[3]);
}

static bool local_ip(const uint8_t *ip) {
    return !memcmp(ip, address, 3) && ip[3] > 1 && ip[3] < 255;
}

static int pool_index(const uint8_t *ip) {
    if (memcmp(ip, address, 3) || ip[3] < WIFI_DHCP_FIRST_HOST ||
            ip[3] >= WIFI_DHCP_FIRST_HOST + WIFI_DHCP_LEASES) return -1;
    return ip[3] - WIFI_DHCP_FIRST_HOST;
}

static bool read_options(const uint8_t *packet, size_t length, dhcp_options *opts) {
    memset(opts, 0, sizeof(*opts));
    size_t at = 240;
    while (at < length) {
        unsigned code = packet[at++];
        if (!code) continue;
        if (code == 255) return opts->type != 0;
        if (at == length) return false;
        unsigned n = packet[at++];
        if (n > length - at) return false;
        const uint8_t *value = packet + at;
        at += n;
        switch (code) {
        case 53:
            if (opts->type || n != 1 || !value[0]) return false;
            opts->type = value[0]; break;
        case 50:
            if (opts->requested || n != 4) return false;
            opts->requested = value; break;
        case 54:
            if (opts->server || n != 4) return false;
            opts->server = value; break;
        case 61:
            if (opts->id || n < 2) return false;
            opts->id = value; opts->id_length = (uint8_t)n; break;
        case 52:
            /* Option-overloaded BOOTP fields and relay agents are outside
             * this small, private hotspot server's scope. Fail closed. */
            return false;
        default: break;
        }
    }
    return false; /* Missing END option. */
}

static bool same_client(const wifi_dhcp_lease *lease, const uint8_t *mac,
        const dhcp_options *opts) {
    if (lease->state != OFFERED && lease->state != BOUND) return false;
    if (lease->id_length != opts->id_length) return false;
    return opts->id ? !memcmp(lease->id, opts->id, opts->id_length)
                    : !memcmp(lease->mac, mac, 6);
}

static void assign(wifi_dhcp_lease *lease, const uint8_t *mac,
        const dhcp_options *opts, uint8_t state, uint32_t expires) {
    memset(lease, 0, sizeof(*lease));
    lease->state = state;
    lease->expires = expires;
    memcpy(lease->mac, mac, 6);
    lease->id_length = opts->id_length;
    if (opts->id) memcpy(lease->id, opts->id, opts->id_length);
}

static size_t option(uint8_t *reply, size_t at, uint8_t code,
        const uint8_t *value, uint8_t size) {
    reply[at++] = code;
    reply[at++] = size;
    memcpy(reply + at, value, size);
    return at + size;
}

static size_t seconds_option(uint8_t *reply, size_t at, uint8_t code, uint32_t seconds) {
    uint8_t bytes[4] = {(uint8_t)(seconds >> 24), (uint8_t)(seconds >> 16),
                       (uint8_t)(seconds >> 8), (uint8_t)seconds};
    return option(reply, at, code, bytes, sizeof(bytes));
}

void wifi_dhcp_init(wifi_dhcp_server *server) {
    memset(server, 0, sizeof(*server));
}

size_t wifi_dhcp_reply(wifi_dhcp_server *server, const uint8_t *request,
        size_t length, uint32_t now_seconds, uint8_t *reply, size_t capacity,
        uint8_t destination[4]) {
    if (!server || !request || !reply || !destination ||
            capacity < WIFI_DHCP_PACKET_MAX || length < 244 ||
            length > WIFI_DHCP_PACKET_MAX) return 0;
    /* Ethernet BOOTREQUEST, no relays, valid unicast client MAC, DHCP cookie. */
    if (request[0] != 1 || request[1] != 1 || request[2] != 6 || request[3] ||
            !zero_ip(request + 24) || (request[28] & 1) ||
            !(request[28] | request[29] | request[30] | request[31] | request[32] | request[33]) ||
            memcmp(request + 236, cookie, sizeof(cookie))) return 0;
    dhcp_options opts;
    if (!read_options(request, length, &opts)) return 0;
    if (opts.server && memcmp(opts.server, address, 4)) return 0;

    int client = -1;
    for (unsigned i = 0; i < WIFI_DHCP_LEASES; ++i) {
        wifi_dhcp_lease *lease = &server->leases[i];
        if (lease->state != FREE && (int32_t)(now_seconds - lease->expires) >= 0)
            memset(lease, 0, sizeof(*lease));
        if (same_client(lease, request + 28, &opts)) client = (int)i;
    }

    uint8_t response = 0;
    int selected = -1;
    if (opts.type == DISCOVER) {
        if (opts.server || !zero_ip(request + 12)) return 0;
        selected = client;
        if (selected < 0 && opts.requested) {
            int wanted = pool_index(opts.requested);
            if (wanted >= 0 && server->leases[wanted].state == FREE) selected = wanted;
        }
        for (unsigned i = 0; selected < 0 && i < WIFI_DHCP_LEASES; ++i)
            if (server->leases[i].state == FREE) selected = (int)i;
        if (selected < 0) return 0;
        /* Rediscovery must not shorten a live binding to the offer timeout. */
        if (server->leases[selected].state != BOUND)
            assign(&server->leases[selected], request + 28, &opts, OFFERED, now_seconds + 60);
        response = OFFER;
    } else if (opts.type == REQUEST) {
        if (opts.requested && !zero_ip(request + 12)) return 0;
        if (opts.server && !opts.requested) return 0;
        const uint8_t *wanted = opts.requested ? opts.requested : request + 12;
        if (zero_ip(wanted)) return 0;
        selected = pool_index(wanted);
        /* A cached address on this subnet after a server restart is unknown,
         * so let the client return to DISCOVER (RFC 2131 section 4.3.2). */
        if (!opts.server && opts.requested && client < 0 && local_ip(wanted)) return 0;
        if (selected < 0 || selected != client) {
            selected = -1;
            response = NAK;
        } else {
            assign(&server->leases[selected], request + 28, &opts, BOUND,
                    now_seconds + WIFI_DHCP_LEASE_SECONDS);
            response = ACK;
        }
    } else if (opts.type == RELEASE) {
        if (!opts.server || opts.requested || client < 0 ||
                pool_index(request + 12) != client) return 0;
        memset(&server->leases[client], 0, sizeof(server->leases[client]));
        return 0;
    } else if (opts.type == DECLINE) {
        if (!opts.server || !opts.requested || !zero_ip(request + 12) ||
                client < 0 || pool_index(opts.requested) != client) return 0;
        /* The client found a duplicate address. Quarantine it for ten minutes. */
        memset(&server->leases[client], 0, sizeof(server->leases[client]));
        server->leases[client].state = DECLINED;
        server->leases[client].expires = now_seconds + 600;
        return 0;
    } else if (opts.type == INFORM) {
        if (opts.requested || !local_ip(request + 12)) return 0;
        response = ACK;
    } else return 0;

    memset(reply, 0, WIFI_DHCP_PACKET_MAX);
    reply[0] = 2; reply[1] = 1; reply[2] = 6;
    memcpy(reply + 4, request + 4, 4); /* xid */
    reply[10] = request[10] & 0x80; /* broadcast flag; all reserved bits zero */
    if (response != NAK) memcpy(reply + 12, request + 12, 4);
    if (selected >= 0) {
        memcpy(reply + 16, address, 4);
        reply[19] = (uint8_t)(WIFI_DHCP_FIRST_HOST + selected);
    }
    memcpy(reply + 28, request + 28, 6);
    memcpy(reply + 236, cookie, 4);
    size_t at = option(reply, 240, 53, &response, 1);
    at = option(reply, at, 54, address, 4);
    if (response != NAK) {
        at = option(reply, at, 1, mask, 4);
        if (opts.type != INFORM) {
            at = seconds_option(reply, at, 51, WIFI_DHCP_LEASE_SECONDS);
            at = seconds_option(reply, at, 58, WIFI_DHCP_LEASE_SECONDS / 2);
            at = seconds_option(reply, at, 59, WIFI_DHCP_LEASE_SECONDS * 7 / 8);
        }
    }
    if (opts.id) at = option(reply, at, 61, opts.id, opts.id_length);
    reply[at++] = 255;
    /* RFC 2131 permits broadcast when pre-configuration unicast is unavailable.
     * This avoids inserting untrusted MACs into lwIP's ARP table just for DHCP. */
    if (response != NAK && !zero_ip(request + 12)) memcpy(destination, request + 12, 4);
    else memset(destination, 255, 4);
    return at < 300 ? 300 : at;
}
