/* SPDX-License-Identifier: MIT */
#ifndef APPLE_USB_INPUT_H
#define APPLE_USB_INPUT_H

#include <stdbool.h>
#include <stdint.h>

void apple_usb_init(void);
void apple_usb_poll(void);
/* Returns one translated press/release. Repeat timing belongs to the caller. */
int apple_usb_next_key(int *pressed, uint8_t *key);
/* Raw HID modifier bits, preserving left/right identity. */
uint8_t apple_usb_modifiers(void);
bool apple_usb_reset_requested(void);
uint32_t apple_usb_gamepad_state(void);

#endif
