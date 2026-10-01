// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum { TB_HOME, TB_UP, TB_DOWN, TB_A, TB_B, TB_C, TB_COUNT };

// A backs out, B confirms, C moves forward. Menus have no horizontal action,
// so C is a second confirm button. HOME is handled separately by the caller.
static inline uint8_t tufty_button_key(unsigned button, bool menu_visible) {
    static const uint8_t keys[TB_COUNT] = {0, 0x0b, 0x0a, 0x1b, 0x0d, 0x15};
    if (button >= TB_COUNT) return 0;
    return menu_visible && button == TB_C ? 0x0d : keys[button];
}
