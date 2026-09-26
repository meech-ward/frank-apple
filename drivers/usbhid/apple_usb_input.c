/*
 * SPDX-License-Identifier: MIT
 * Original Apple II input adapter. Interface requirements come from main.c,
 * the MIT usbhid.h producer and TinyUSB's MIT HID definitions/ASCII table.
 * The former GPL keyboard adapter is not a source for this implementation.
 */
#include "apple_usb_input.h"
#include "usbhid.h"
#include "tusb.h"
#include "input_controls.h"
#include "nespad/nespad.h"
#include <string.h>

static const uint8_t ascii[][2] = { HID_KEYCODE_TO_ASCII };
static bool held[256];
static uint8_t identity[256];
static bool caps_lock = true;
static unsigned release_cursor = 256;

static bool report_has(const usbhid_keyboard_state_t *state, uint8_t usage) {
    for (unsigned i = 0; i < 6; ++i)
        if (state->keycode[i] == usage) return true;
    return false;
}

static bool translate(uint8_t usage, uint8_t modifiers, uint8_t *key) {
    bool shift = (modifiers & (KEYBOARD_MODIFIER_LEFTSHIFT |
                              KEYBOARD_MODIFIER_RIGHTSHIFT)) != 0;
    bool control = (modifiers & (KEYBOARD_MODIFIER_LEFTCTRL |
                                KEYBOARD_MODIFIER_RIGHTCTRL)) != 0;
    if (usage >= HID_KEY_A && usage <= HID_KEY_Z) {
        *key = control ? usage - HID_KEY_A + 1 :
            (caps_lock || shift ? 'A' : 'a') + usage - HID_KEY_A;
        return true;
    }
    switch (usage) {
        case HID_KEY_F11: *key = 0xFB; return true;
        case HID_KEY_PAGE_UP: *key = 0xFD; return true;
        case HID_KEY_PAGE_DOWN: *key = 0xFE; return true;
        case HID_KEY_ARROW_LEFT: *key = 0x08; return true;
        case HID_KEY_ARROW_RIGHT: *key = 0x15; return true;
        case HID_KEY_ARROW_UP: *key = 0x0B; return true;
        case HID_KEY_ARROW_DOWN: *key = 0x0A; return true;
        case HID_KEY_BACKSPACE: *key = 0x08; return true;
        case HID_KEY_DELETE:
            if (control && (modifiers & (KEYBOARD_MODIFIER_LEFTALT |
                                       KEYBOARD_MODIFIER_RIGHTALT))) return false;
            *key = 0x7F; return true;
        default: break;
    }
    if (usage >= sizeof(ascii) / sizeof(ascii[0])) return false;
    /* Keypad symbols/numbers do not change when Shift is held. */
    bool keypad = usage >= HID_KEY_KEYPAD_DIVIDE && usage <= HID_KEY_KEYPAD_EQUAL;
    uint8_t ch = ascii[usage][keypad ? 0 : shift];
    if (!ch) return false;
    if (control) {
        if (ch == ' ') ch = 0;
        else if (ch >= '@' && ch <= '_') ch &= 0x1F;
    }
    *key = ch;
    return true;
}

void apple_usb_init(void) {
    memset(held, 0, sizeof(held));
    memset(identity, 0, sizeof(identity));
    caps_lock = true;
    release_cursor = 256;
    usb_turbo_momentary = false;
    usbhid_init();
}

void apple_usb_poll(void) {
    usbhid_task();
    usbhid_keyboard_state_t state;
    usbhid_get_keyboard_state(&state);
    usb_turbo_momentary = report_has(&state, HID_KEY_F12);
}

int apple_usb_next_key(int *pressed, uint8_t *key) {
    if (!pressed || !key) return 0;
    for (;;) {
        /* Queue overflow explicitly releases delivered identities before replay. */
        while (release_cursor < 256) {
            unsigned usage = release_cursor++;
            if (held[usage]) {
                held[usage] = false;
                *pressed = 0;
                *key = identity[usage];
                return 1;
            }
        }
        uint8_t usage, modifiers;
        int down;
        if (!usbhid_get_key_event(&usage, &down, &modifiers)) return 0;
        if (down < 0) {
            release_cursor = 0;
            continue;
        }
        if (!usage) continue;
        if (!down) {
            if (!held[usage]) continue;
            held[usage] = false;
            *pressed = 0;
            *key = identity[usage];
            return 1;
        }
        if (held[usage]) continue;
        if (usage == HID_KEY_F9) { if (down == 1) show_speed = !show_speed; continue; }
        if (usage == HID_KEY_SCROLL_LOCK) { if (down == 1) turbo_latched = !turbo_latched; continue; }
        if (usage == HID_KEY_CAPS_LOCK) { if (down == 1) caps_lock = !caps_lock; continue; }
        /* The caller treats F11 as a menu toggle, so recovery must not repeat it. */
        if (usage == HID_KEY_F11 && down == 2) continue;
        if (!translate(usage, modifiers, key)) continue;
        held[usage] = true;
        identity[usage] = *key;
        *pressed = 1;
        return 1;
    }
}

uint8_t apple_usb_modifiers(void) {
    usbhid_keyboard_state_t state;
    usbhid_get_keyboard_state(&state);
    return state.modifier;
}

bool apple_usb_reset_requested(void) {
    usbhid_keyboard_state_t state;
    usbhid_get_keyboard_state(&state);
    return (state.modifier & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL)) &&
           (state.modifier & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT)) &&
           report_has(&state, HID_KEY_DELETE);
}

uint32_t apple_usb_gamepad_state(void) {
    usbhid_gamepad_state_t pad;
    usbhid_get_gamepad_state(&pad);
    uint32_t result = 0;
    if (pad.connected) {
        if ((pad.dpad & 1) || pad.axis_y < -32) result |= DPAD_UP;
        if ((pad.dpad & 2) || pad.axis_y > 32) result |= DPAD_DOWN;
        if ((pad.dpad & 4) || pad.axis_x < -32) result |= DPAD_LEFT;
        if ((pad.dpad & 8) || pad.axis_x > 32) result |= DPAD_RIGHT;
        static const uint32_t buttons[] = {
            DPAD_A, DPAD_B, DPAD_Y, DPAD_X, DPAD_LT, DPAD_RT, DPAD_START, DPAD_SELECT
        };
        for (unsigned i = 0; i < 8; ++i)
            if (pad.buttons & (1u << i)) result |= buttons[i];
    }
    usbhid_keyboard_state_t state;
    usbhid_get_keyboard_state(&state);
    if (state.modifier & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_LEFTGUI)) result |= DPAD_A;
    if (state.modifier & (KEYBOARD_MODIFIER_RIGHTALT | KEYBOARD_MODIFIER_RIGHTGUI)) result |= DPAD_B;
    for (unsigned i = 0; i < 6; ++i) {
        switch (state.keycode[i]) {
            /* Documented keypad directions; keypad digits also remain typable. */
            case HID_KEY_KEYPAD_2:
            case HID_KEY_KEYPAD_5: result |= DPAD_DOWN; break;
            case HID_KEY_KEYPAD_4: result |= DPAD_LEFT; break;
            case HID_KEY_KEYPAD_6: result |= DPAD_RIGHT; break;
            case HID_KEY_KEYPAD_8: result |= DPAD_UP; break;
            default: break;
        }
    }
    return result;
}
