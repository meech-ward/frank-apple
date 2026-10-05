#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Shared keystroke FIFO (implemented in main.c). Text is mapped at enqueue:
 * LF -> CR, DEL -> backspace, lowercase -> uppercase. The frame loop waits for
 * the Apple keyboard latch, with a 500ms pause after Return for BASIC to parse
 * the line. This is keyboard input, not execution completion/acknowledgment.
 * The legacy push drops bytes that do not fit; new callers should use try_push. */
void typing_push(const uint8_t *s, size_t n);
/* All-or-nothing text enqueue, with LF/case mapping. Core 0 only. */
bool typing_try_push(const uint8_t *s, size_t n);
/* Literal text for word processors; preserves case. */
bool typing_try_literal(const uint8_t *s, size_t n);
/* Queue one key with Open Apple held until the key has been consumed. */
bool typing_try_apple(uint8_t key);
size_t typing_pending(void);
/* Named remote key / menu actions, called outside lwIP callbacks on core 0. */
void remote_control_key(uint8_t key);
/* Backpressure for continuous terminal input. Interrupt/menu actions always
 * take priority; ordinary keys wait when the shared keyboard FIFO is full. */
bool remote_control_try_key(uint8_t key);
size_t remote_control_screen(char *out, char *inverse, size_t cap);
bool remote_control_graphics(void);
bool remote_control_basic_prompt(void);
unsigned remote_control_columns(void);
