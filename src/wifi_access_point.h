/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include "wifi_config.h"

/* Call after successful cyw43_arch_init(), instead of enabling station mode.
 * Starts WPA2 AP on channel 6 at 192.168.4.1/24 and a bounded DHCP server.
 * Existing cyw43_arch_poll() services it; no additional poll/thread needed.
 * Use only from the same core/context as the firmware's other lwIP calls. */
bool wifi_access_point_start(const wifi_config *config);
void wifi_access_point_stop(void);
bool wifi_access_point_active(void);
