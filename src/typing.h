#pragma once

#include <stddef.h>
#include <stdint.h>

/* Shared keystroke FIFO (implemented in main.c).
 * Serial input and the realtime channel both push raw bytes here;
 * the frame loop drains one byte per frame through the serial
 * mapping (LF -> CR, DEL -> backspace, lowercase -> uppercase)
 * into mii_keypress. Bytes that do not fit are dropped. */
void typing_push(const uint8_t *s, size_t n);
