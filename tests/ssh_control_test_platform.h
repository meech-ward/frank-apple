// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t address; } ip4_addr_t;
struct netif { ip4_addr_t ip; };
extern struct netif *netif_default;
#define netif_ip4_addr(interface) (&(interface)->ip)
#define ip4_addr_isany_val(value) ((value).address == 0)
const char *ip4addr_ntoa(const ip4_addr_t *address);
uint64_t get_rand_64(void);
uint64_t time_us_64(void);
bool disk_ui_is_visible(void);
const char *netcard_wifi_status(void);
#define MII_DEBUG_PRINTF(...) do { } while (0)
