// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SSH_TERMINAL_COLUMNS 80
#define SSH_TERMINAL_ROWS 24
#define SSH_TERMINAL_SCREEN_CAP ((SSH_TERMINAL_COLUMNS + 1) * SSH_TERMINAL_ROWS + 1)
#define SSH_TERMINAL_TX_CAP 768
#define SSH_TERMINAL_INPUT_BUDGET 256
#define SSH_TERMINAL_OUTPUT_BUDGET 512
#define SSH_TERMINAL_ESCAPE_MS 150
#define SSH_TERMINAL_REFRESH_MS 100

typedef struct {
    /* Copy/accept a prefix, returning its length. Zero means retry later.
     * The buffer belongs to this adapter and is reused after acceptance. */
    size_t (*send)(void *context, const uint8_t *data, size_t length);
    /* Accept one Apple key or return false without side effects. Ctrl-C should
     * be accepted even when the normal typing queue is full. Core 0 only. */
    bool (*key)(void *context, uint8_t key);
    /* Same newline-separated text and '0'/'1' inverse mask as
     * remote_control_screen(). The callback also returns 40/80-column mode
     * and whether the Apple is showing graphics. Zero means retry later. */
    size_t (*snapshot)(void *context, char *text, char *inverse, size_t capacity,
                       unsigned *columns, bool *graphics);
} ssh_terminal_callbacks_t;

/* One instance per device, not per packet. No heap, platform calls, or global
 * state. The fixed buffers consume about 9 KiB; do not put this on a Pico stack.
 * All calls run on the emulator's core, outside lwIP callbacks. */
typedef struct {
    ssh_terminal_callbacks_t callbacks;
    void *context;
    uint16_t previous[SSH_TERMINAL_COLUMNS * SSH_TERMINAL_ROWS];
    char text[SSH_TERMINAL_SCREEN_CAP];
    char inverse[SSH_TERMINAL_SCREEN_CAP];
    uint16_t row_offset[SSH_TERMINAL_ROWS];
    uint16_t row_length[SSH_TERMINAL_ROWS];
    uint8_t tx[SSH_TERMINAL_TX_CAP];
    size_t tx_length, tx_offset;
    unsigned host_columns, host_rows;
    unsigned columns, last_columns, render_columns, render_rows, scan_cell;
    uint32_t next_refresh, sequence_time;
    uint8_t sequence[16], sequence_length, sequence_flush;
    uint8_t pending_key;
    bool pending_input, sequence_literal, sequence_discard, paste, skip_lf;
    bool active, frame_active, previous_valid, redraw_requested;
    bool graphics, last_graphics, footer_needed, inverse_active, positioned, frame_had_output;
} ssh_terminal_t;

void ssh_terminal_init(ssh_terminal_t *terminal,
                       const ssh_terminal_callbacks_t *callbacks, void *context);
void ssh_terminal_open(ssh_terminal_t *terminal, uint32_t now_ms);
void ssh_terminal_resize(ssh_terminal_t *terminal, unsigned columns, unsigned rows);

/* Returns exactly the bytes consumed, including at most one retained key.
 * Keep the remainder and retry after poll(). This provides input backpressure
 * without losing a pasted program. Each call consumes at most INPUT_BUDGET.
 * A Ctrl-C present in that prefix cancels earlier pending input immediately.
 * ASCII case is preserved: use Caps Lock for Applesoft BASIC commands. */
size_t ssh_terminal_receive(ssh_terminal_t *terminal, const uint8_t *data,
                            size_t length, uint32_t now_ms);
void ssh_terminal_poll(ssh_terminal_t *terminal, uint32_t now_ms);

/* Graceful close: queue restoration of the user's terminal, then keep polling
 * until finished() before closing the SSH channel. On a lost connection the
 * transport may discard the instance; open() always starts a fresh mirror.
 * close() does not stop or reset the Apple II. */
void ssh_terminal_close(ssh_terminal_t *terminal);
bool ssh_terminal_finished(const ssh_terminal_t *terminal);
