/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "wifi_config.h"

/* Core 0, outside lwIP callbacks. Configuration has static lifetime. */
void ssh_control_init(const wifi_config *config);
void ssh_control_poll(bool network_ready);
void ssh_control_toggle(void);
bool ssh_control_enabled(void);
const char *ssh_control_label(void);
void ssh_control_address(char *out, size_t capacity);
