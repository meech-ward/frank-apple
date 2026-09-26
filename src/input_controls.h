// SPDX-License-Identifier: MIT
// Original downstream control state, independent of a keyboard transport.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
extern bool turbo_latched;
extern bool turbo_momentary; // Optional PS/2 input.
extern bool usb_turbo_momentary;
extern bool show_speed;
#ifdef __cplusplus
}
#endif

static inline bool input_turbo_active(void) {
    return turbo_latched || turbo_momentary || usb_turbo_momentary;
}
static inline bool input_speed_visible(void) { return show_speed; }
