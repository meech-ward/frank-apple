/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "usbhid.h"
#include "apple_usb_input.h"
#include "input_controls.h"
#include "nespad/nespad.h"

static uint8_t protocol = HID_ITF_PROTOCOL_KEYBOARD;
static uint8_t descriptor_usage, descriptor_id, descriptor_second_usage;
uint8_t tuh_hid_interface_protocol(uint8_t device, uint8_t instance) {
    (void)device; (void)instance; return protocol;
}
bool tuh_hid_receive_report(uint8_t device, uint8_t instance) {
    (void)device; (void)instance; return true;
}
uint8_t tuh_hid_parse_report_descriptor(tuh_hid_report_info_t *info,
    uint8_t count, const uint8_t *report, uint16_t len) {
    (void)count; (void)report; (void)len;
    if (!descriptor_usage) return 0;
    info[0] = (tuh_hid_report_info_t){.report_id = descriptor_id,
        .usage_page = HID_USAGE_PAGE_DESKTOP, .usage = descriptor_usage};
    if (descriptor_second_usage) {
        info[1] = (tuh_hid_report_info_t){.report_id = descriptor_id + 1,
            .usage_page = HID_USAGE_PAGE_DESKTOP, .usage = descriptor_second_usage};
        return 2;
    }
    return 1;
}
bool tuh_init(uint8_t rhport) { (void)rhport; return true; }
void tuh_task(void) {}

static void report(uint8_t mods, uint8_t a, uint8_t b) {
    hid_keyboard_report_t r = {.modifier = mods, .keycode = {a, b}};
    tuh_hid_report_received_cb(1, 0, (const uint8_t *)&r, sizeof(r));
    apple_usb_poll();
}
static void expect(int pressed, uint8_t ch) {
    int actual_pressed = -10;
    uint8_t actual_ch = 0;
    assert(apple_usb_next_key(&actual_pressed, &actual_ch));
    assert(actual_pressed == pressed);
    assert(actual_ch == ch);
}
static void empty(void) {
    int down; uint8_t key;
    assert(!apple_usb_next_key(&down, &key));
}
static void fresh(void) {
    protocol = HID_ITF_PROTOCOL_KEYBOARD;
    descriptor_usage = descriptor_id = descriptor_second_usage = 0;
    apple_usb_init();
    turbo_latched = turbo_momentary = show_speed = false;
    tuh_hid_mount_cb(1, 0, NULL, 0);
}
static void tap(uint8_t usage, uint8_t mods, uint8_t expected) {
    report(mods, usage, 0); expect(1, expected);
    report(0, 0, 0); expect(0, expected); empty();
}
int main(void) {
    fresh();
    tap(HID_KEY_A, 0, 'A');
    tap(HID_KEY_Z, KEYBOARD_MODIFIER_RIGHTCTRL, 26);
    tap(HID_KEY_1, KEYBOARD_MODIFIER_LEFTSHIFT, '!');
    tap(HID_KEY_BRACKET_LEFT, KEYBOARD_MODIFIER_LEFTCTRL, 27);
    tap(HID_KEY_2, KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_LEFTCTRL, 0);
    tap(HID_KEY_ENTER, 0, '\r');
    tap(HID_KEY_ESCAPE, 0, 27);
    tap(HID_KEY_TAB, 0, '\t');
    tap(HID_KEY_BACKSPACE, 0, 8);
    tap(HID_KEY_DELETE, 0, 127);
    tap(HID_KEY_ARROW_LEFT, 0, 8);
    tap(HID_KEY_ARROW_RIGHT, 0, 21);
    tap(HID_KEY_ARROW_UP, 0, 11);
    tap(HID_KEY_ARROW_DOWN, 0, 10);
    tap(HID_KEY_F11, 0, 0xFB);
    tap(HID_KEY_PAGE_UP, 0, 0xFD);
    tap(HID_KEY_PAGE_DOWN, 0, 0xFE);
    tap(HID_KEY_KEYPAD_7, KEYBOARD_MODIFIER_LEFTSHIFT, '7');
    tap(HID_KEY_KEYPAD_ADD, 0, '+');
    tap(HID_KEY_KEYPAD_ENTER, 0, '\r');

    /* Snapshot modifiers at arrival, even when all reports precede consumption. */
    report(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_1, 0);
    report(0, HID_KEY_1, 0);
    report(0, 0, 0);
    expect(1, '!'); expect(0, '!'); empty();
    report(KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_A, 0);
    expect(1, 1);
    report(0, HID_KEY_A, 0); empty();
    report(0, 0, 0); expect(0, 1);

    /* Caps Lock starts on for BASIC, but can enable lowercase typing. */
    report(0, HID_KEY_CAPS_LOCK, 0); empty();
    report(0, 0, 0); empty();
    tap(HID_KEY_A, 0, 'a');
    tap(HID_KEY_A, KEYBOARD_MODIFIER_RIGHTSHIFT, 'A');
    report(0, HID_KEY_CAPS_LOCK, 0); empty();
    report(0, 0, 0); empty();

    /* Modifier identity must survive releasing one of two modifiers. */
    report(KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT, 0, 0);
    assert((apple_usb_gamepad_state() & (DPAD_A | DPAD_B)) == (DPAD_A | DPAD_B));
    report(KEYBOARD_MODIFIER_RIGHTALT, 0, 0);
    assert(apple_usb_modifiers() == KEYBOARD_MODIFIER_RIGHTALT);
    assert(apple_usb_gamepad_state() == DPAD_B);
    report(KEYBOARD_MODIFIER_LEFTGUI | KEYBOARD_MODIFIER_RIGHTGUI, 0, 0);
    assert(apple_usb_gamepad_state() == (DPAD_A | DPAD_B));
    report(0, 0, 0); empty();

    report(0, HID_KEY_F9, 0); empty(); assert(show_speed);
    report(0, HID_KEY_F9, 0); empty(); assert(show_speed);
    report(0, 0, 0); empty();
    report(0, HID_KEY_F9, 0); empty(); assert(!show_speed);
    report(0, HID_KEY_SCROLL_LOCK, HID_KEY_F12); empty();
    assert(turbo_latched && usb_turbo_momentary && input_turbo_active());
    report(0, 0, 0); empty(); assert(turbo_latched && !usb_turbo_momentary);
    report(0, HID_KEY_SCROLL_LOCK, 0); empty(); assert(!turbo_latched);
    report(0, 0, 0); empty();
    turbo_momentary = true; apple_usb_poll(); assert(input_turbo_active());
    turbo_momentary = false;

    report(KEYBOARD_MODIFIER_RIGHTCTRL | KEYBOARD_MODIFIER_LEFTALT, HID_KEY_DELETE, 0);
    assert(apple_usb_reset_requested()); empty();
    report(0, 0, 0); assert(!apple_usb_reset_requested()); empty();

    /* Unplug when modifiers, typing repeat and turbo are held. */
    report(KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_LEFTALT, HID_KEY_1, HID_KEY_F12);
    expect(1, '!'); empty(); assert(usb_turbo_momentary);
    protocol = HID_ITF_PROTOCOL_NONE; /* The stack may have forgotten the interface. */
    tuh_hid_umount_cb(1, 0); apple_usb_poll();
    assert(!usb_turbo_momentary && !apple_usb_modifiers() && !apple_usb_gamepad_state());
    expect(0, '!'); empty();

    fresh();
    report(0, HID_KEY_A, 0); expect(1, 'A');
    report(0, 1, 1); empty(); /* Boot keyboard rollover must not release A. */
    report(0, 0, 0); expect(0, 'A'); empty();

    /* Backpressure must release already-delivered keys and recover current keys. */
    report(0, HID_KEY_A, 0); expect(1, 'A');
    for (unsigned i = 0; i < 50; ++i) report(0, i % 2 ? HID_KEY_B : 0, 0);
    report(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_1, 0);
    expect(0, 'A'); expect(1, '!'); empty();
    report(0, 0, 0); expect(0, '!'); empty();

    /* Overflow replays held keys, but must not toggle controls a second time. */
    report(0, HID_KEY_F9, 0); empty(); assert(show_speed);
    for (unsigned i = 0; i < 50; ++i)
        report(0, HID_KEY_F9, i % 2 ? HID_KEY_A : 0);
    report(0, HID_KEY_F9, 0); empty(); assert(show_speed);
    report(0, 0, 0); empty();

    report(0, HID_KEY_F11, 0); expect(1, 0xFB); empty();
    for (unsigned i = 0; i < 50; ++i)
        report(0, HID_KEY_F11, i % 2 ? HID_KEY_A : 0);
    report(0, HID_KEY_F11, 0);
    expect(0, 0xFB); empty(); // Recovery cannot toggle the caller's menu again.
    report(0, 0, 0); empty();

    /* Retained seven-byte gamepad report normalization and disconnect. */
    protocol = HID_ITF_PROTOCOL_NONE;
    uint8_t pad[] = {0, 0, 0, 0x00, 0xFF, 0xF0, 0x33};
    tuh_hid_report_received_cb(2, 0, pad, sizeof(pad));
    assert(apple_usb_gamepad_state() == (DPAD_LEFT | DPAD_DOWN | DPAD_A | DPAD_B |
           DPAD_Y | DPAD_X | DPAD_LT | DPAD_RT | DPAD_START | DPAD_SELECT));
    uint32_t held_pad = apple_usb_gamepad_state();
    tuh_hid_umount_cb(3, 0); // An unrelated generic interface is not the pad owner.
    assert(apple_usb_gamepad_state() == held_pad);
    tuh_hid_umount_cb(2, 0); assert(apple_usb_gamepad_state() == 0);
    fresh();
    report(0, HID_KEY_KEYPAD_9, HID_KEY_KEYPAD_0);
    assert(apple_usb_gamepad_state() == 0); // Undocumented keys have no joystick action.
    expect(1, '9'); expect(1, '0'); empty();
    report(0, HID_KEY_KEYPAD_5, HID_KEY_KEYPAD_6);
    assert(apple_usb_gamepad_state() == (DPAD_DOWN | DPAD_RIGHT));
    expect(0, '9'); expect(0, '0'); expect(1, '5'); expect(1, '6'); empty();
    report(0, HID_KEY_KEYPAD_8, HID_KEY_KEYPAD_4);
    assert(apple_usb_gamepad_state() == (DPAD_UP | DPAD_LEFT));
    expect(0, '5'); expect(0, '6'); expect(1, '8'); expect(1, '4'); empty();

    /* Generic desktop keyboards carry a report ID before the boot-shaped data. */
    fresh();
    protocol = HID_ITF_PROTOCOL_NONE;
    descriptor_usage = HID_USAGE_DESKTOP_KEYBOARD;
    descriptor_id = 7;
    tuh_hid_mount_cb(2, 1, NULL, 0);
    uint8_t generic[] = {7, KEYBOARD_MODIFIER_RIGHTALT | KEYBOARD_MODIFIER_LEFTSHIFT,
                        0, HID_KEY_1, HID_KEY_F12, 0, 0, 0, 0};
    tuh_hid_report_received_cb(2, 1, generic, sizeof(generic));
    apple_usb_poll(); expect(1, '!'); empty();
    assert(usb_turbo_momentary && apple_usb_gamepad_state() == DPAD_B);
    /* An unrelated interface unplug must not release this keyboard. */
    tuh_hid_umount_cb(3, 1); apple_usb_poll(); empty();
    assert(usb_turbo_momentary);
    /* Incomplete reports must not be read or change the held key. */
    tuh_hid_report_received_cb(2, 1, generic, 2); empty();
    tuh_hid_umount_cb(2, 1); apple_usb_poll();
    expect(0, '!'); empty();
    assert(!apple_usb_modifiers() && !usb_turbo_momentary && !apple_usb_gamepad_state());
    /* One composite interface owns keyboard and gamepad reports independently. */
    fresh();
    protocol = HID_ITF_PROTOCOL_NONE;
    descriptor_usage = HID_USAGE_DESKTOP_KEYBOARD;
    descriptor_second_usage = HID_USAGE_DESKTOP_GAMEPAD;
    descriptor_id = 7;
    tuh_hid_mount_cb(2, 1, NULL, 0);
    tuh_hid_report_received_cb(2, 1, generic, sizeof(generic));
    uint8_t composite_pad[] = {8, 0, 0, 0, 0x00, 0xFF, 0x20, 0x20};
    tuh_hid_report_received_cb(2, 1, composite_pad, sizeof(composite_pad));
    apple_usb_poll(); expect(1, '!'); empty();
    assert(apple_usb_gamepad_state() == (DPAD_LEFT | DPAD_DOWN | DPAD_A | DPAD_B | DPAD_START));
    assert(usb_turbo_momentary && usbhid_gamepad_connected());
    tuh_hid_umount_cb(2, 1); apple_usb_poll();
    expect(0, '!'); empty();
    assert(!usb_turbo_momentary && !apple_usb_modifiers() && !apple_usb_gamepad_state());
    assert(!usbhid_keyboard_connected() && !usbhid_gamepad_connected());
    puts("USB input: translation, controls, snapshots, disconnect, overflow, gamepad passed");
    return 0;
}
