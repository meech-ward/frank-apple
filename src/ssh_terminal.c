// SPDX-License-Identifier: MIT
#include "ssh_terminal.h"

#include <stdio.h>
#include <string.h>

/* Keep the normal terminal buffer and its cursor/wrap/paste modes. An abrupt
 * WiFi loss or OpenSSH ~. escape cannot leave private terminal modes stuck. */
static const char terminal_enter[] = "\033[0m\033[2J\033[1;1H";
static const char terminal_leave[] = "\033[0m\r\n";
static const uint8_t paste_end[] = "\033[201~";

static bool elapsed(uint32_t now, uint32_t start, uint32_t delay) {
    return (uint32_t)(now - start) >= delay;
}

static void reset_input(ssh_terminal_t *t) {
    t->sequence_length = t->sequence_flush = 0;
    t->sequence_literal = t->sequence_discard = t->pending_input = t->paste = t->skip_lf = false;
}

void ssh_terminal_init(ssh_terminal_t *t,
                       const ssh_terminal_callbacks_t *callbacks, void *context) {
    memset(t, 0, sizeof(*t));
    if (callbacks) t->callbacks = *callbacks;
    t->context = context;
    t->host_columns = 80;
    t->host_rows = 24;
}

void ssh_terminal_open(ssh_terminal_t *t, uint32_t now_ms) {
    reset_input(t);
    t->active = true;
    t->frame_active = t->previous_valid = t->inverse_active = false;
    t->redraw_requested = true;
    t->next_refresh = now_ms;
    t->tx_offset = 0;
    t->tx_length = sizeof(terminal_enter) - 1;
    memcpy(t->tx, terminal_enter, t->tx_length);
}

void ssh_terminal_resize(ssh_terminal_t *t, unsigned columns, unsigned rows) {
    /* SSH permits a zero-sized PTY request. Use a normal terminal until its
     * first window-change request instead of hiding the screen. */
    if (!columns) columns = 80;
    if (!rows) rows = 24;
    if (columns == t->host_columns && rows == t->host_rows) return;
    t->host_columns = columns;
    t->host_rows = rows;
    t->redraw_requested = true;
}

static bool deliver_pending(ssh_terminal_t *t) {
    if (!t->pending_input) return true;
    if (!t->callbacks.key || !t->callbacks.key(t->context, t->pending_key)) return false;
    t->pending_input = false;
    return true;
}

/* Even an unaccepted key is retained, so its source byte can be acknowledged. */
static void deliver_key(ssh_terminal_t *t, uint8_t key) {
    if (!t->callbacks.key || !t->callbacks.key(t->context, key)) {
        t->pending_key = key;
        t->pending_input = true;
    }
}

static void deliver_character(ssh_terminal_t *t, uint8_t key) {
    if (key == '\n' && t->skip_lf) {
        t->skip_lf = false;
        return;
    }
    t->skip_lf = key == '\r';
    if (key == '\n') key = '\r';
    else if (key == 0x7f) key = 0x08;
    /* NUL is not an Apple key; it can make GETLN swallow the next command.
     * Apple II text input is seven-bit. Ignore unsupported UTF-8 bytes. */
    if (key && key < 0x80) deliver_key(t, key);
}

static bool flush_literal_sequence(ssh_terminal_t *t) {
    while (t->sequence_flush < t->sequence_length) {
        if (!deliver_pending(t)) return false;
        deliver_character(t, t->sequence[t->sequence_flush++]);
    }
    t->sequence_length = t->sequence_flush = 0;
    t->sequence_literal = false;
    return deliver_pending(t);
}

/* Return 0 for unsupported escape sequences. They are consumed as a unit,
 * never injected as their trailing letters into a BASIC program. */
static uint8_t sequence_key(const uint8_t *sequence, size_t length) {
    if (length < 3 || (sequence[1] != '[' && sequence[1] != 'O')) return 0;
    switch (sequence[length - 1]) {
        case 'A': return 0x0b;
        case 'B': return 0x0a;
        case 'C': return 0x15;
        case 'D': return 0x08;
        case '~':
            if (length == 4 && sequence[2] == '3') return 0x08; // Delete
            if (length == 5 && sequence[2] == '2' && sequence[3] == '3')
                return 0x1d; // F11: badge menu
            return 0;
        default: return 0;
    }
}

static void expire_sequence(ssh_terminal_t *t, uint32_t now) {
    if (t->sequence_discard && elapsed(now, t->sequence_time, 1000))
        t->sequence_discard = false;
    if (!t->sequence_length || t->sequence_literal) return;
    if (t->paste) {
        if (elapsed(now, t->sequence_time, 1000)) t->sequence_literal = true;
    } else if (t->sequence_length == 1) {
        if (elapsed(now, t->sequence_time, SSH_TERMINAL_ESCAPE_MS))
            t->sequence_literal = true;
    } else if (elapsed(now, t->sequence_time, 1000)) {
        /* An abandoned CSI/SS3 sequence is not a printable command. */
        t->sequence_length = 0;
    }
}

size_t ssh_terminal_receive(ssh_terminal_t *t, const uint8_t *data,
                            size_t length, uint32_t now) {
    if (!t->active || !data) return 0;
    if (length > SSH_TERMINAL_INPUT_BUDGET) length = SSH_TERMINAL_INPUT_BUDGET;
    size_t used = 0;
    /* A stop key must not sit behind a full typing queue. The callback clears
     * the emulator FIFO too. Take the last stop in this offered prefix so
     * preceding pasted text is cancelled, not replayed after Ctrl-C. */
    for (size_t i = 0; i < length; ++i) if (data[i] == 3) used = i + 1;
    if (used) {
        reset_input(t);
        deliver_key(t, 3);
    }
    expire_sequence(t, now);
    while (used < length) {
        if (!deliver_pending(t)) break;
        if (t->sequence_literal && !flush_literal_sequence(t)) break;
        uint8_t key = data[used];
        /* Only adjacent CR/LF bytes form one Return. A terminal escape or
         * bracketed-paste boundary between them starts a distinct key. */
        if (key == 0x1b) t->skip_lf = false;
        if (t->sequence_discard) {
            if (key >= 0x40 && key <= 0x7e) t->sequence_discard = false;
            t->sequence_time = now;
            ++used;
        } else if (t->paste) {
            if (t->sequence_length || key == 0x1b) {
                if (key == paste_end[t->sequence_length]) {
                    t->sequence[t->sequence_length++] = key;
                    t->sequence_time = now;
                    ++used;
                    if (t->sequence_length == sizeof(paste_end) - 1) {
                        t->paste = false;
                        t->sequence_length = 0;
                    }
                } else {
                    /* The held prefix was pasted text, not the end marker.
                     * Flush it with backpressure, then reconsider this byte. */
                    t->sequence_literal = true;
                }
                continue;
            }
            deliver_character(t, key);
            ++used;
        } else if (!t->sequence_length) {
            if (key == 0x1b) {
                t->sequence[0] = key;
                t->sequence_length = 1;
                t->sequence_time = now;
            } else deliver_character(t, key);
            ++used;
        } else if (t->sequence_length == 1 && key != '[' && key != 'O') {
            /* Alt+letter is Escape followed by that letter on the Apple. */
            t->sequence_literal = true;
        } else if (key == 0x1b) {
            t->sequence[0] = key;
            t->sequence_length = 1;
            t->sequence_time = now;
            ++used;
        } else {
            if (t->sequence_length == sizeof(t->sequence)) {
                /* Bound malformed input, discarding through its final byte. */
                t->sequence_length = 0;
                t->sequence_discard = !(key >= 0x40 && key <= 0x7e);
                t->sequence_time = now;
                ++used;
                continue;
            }
            t->sequence[t->sequence_length++] = key;
            t->sequence_time = now;
            ++used;
            if (t->sequence_length > 2 && key >= 0x40 && key <= 0x7e) {
                if (t->sequence_length == 6 &&
                    !memcmp(t->sequence, "\033[200~", 6)) t->paste = true;
                else {
                    uint8_t apple_key = sequence_key(t->sequence, t->sequence_length);
                    if (apple_key) deliver_key(t, apple_key);
                }
                t->sequence_length = 0;
            }
        }
    }
    return used;
}

static void append(ssh_terminal_t *t, const char *data, size_t length) {
    memcpy(t->tx + t->tx_length, data, length);
    t->tx_length += length;
}

static bool snapshot(ssh_terminal_t *t, uint32_t now) {
    if (!t->callbacks.snapshot) return false;
    if (!t->redraw_requested && (int32_t)(now - t->next_refresh) < 0) return false;
    unsigned columns = 40;
    bool graphics = false;
    memset(t->inverse, '0', sizeof(t->inverse));
    size_t length = t->callbacks.snapshot(t->context, t->text, t->inverse,
                                         sizeof(t->text), &columns, &graphics);
    if (!length) {
        t->next_refresh = now + SSH_TERMINAL_REFRESH_MS;
        return false;
    }
    if (length >= sizeof(t->text)) length = sizeof(t->text) - 1;
    columns = columns == 80 ? 80 : 40;
    bool clear = t->redraw_requested || !t->previous_valid || columns != t->last_columns;
    t->columns = columns;
    t->graphics = graphics;
    t->render_columns = columns < t->host_columns ? columns : t->host_columns;
    t->render_rows = t->host_rows < SSH_TERMINAL_ROWS ? t->host_rows : SSH_TERMINAL_ROWS;
    t->footer_needed = t->host_rows >= 25 && (clear || graphics != t->last_graphics);
    t->scan_cell = 0;
    t->positioned = false;
    t->frame_active = true;
    t->frame_had_output = clear;
    t->redraw_requested = false;
    t->next_refresh = now + SSH_TERMINAL_REFRESH_MS;
    if (clear) {
        append(t, "\033[0m\033[2J", 8);
        t->inverse_active = false;
        t->previous_valid = false;
    }
    /* Menus have ragged lines; Apple VRAM snapshots have fixed-width lines.
     * Record bounded spans so both formats render without a second image. */
    size_t offset = 0;
    for (unsigned row = 0; row < SSH_TERMINAL_ROWS; ++row) {
        t->row_offset[row] = (uint16_t)offset;
        while (offset < length && t->text[offset] != '\n') ++offset;
        size_t row_length = offset - t->row_offset[row];
        if (row_length && t->text[offset - 1] == '\r') --row_length;
        if (row_length > columns) row_length = columns;
        t->row_length[row] = (uint16_t)row_length;
        if (offset < length) ++offset;
    }
    return true;
}

static uint16_t screen_cell(const ssh_terminal_t *t, unsigned row, unsigned column) {
    if (column >= t->row_length[row]) return ' ';
    size_t offset = t->row_offset[row] + column;
    unsigned char ch = (unsigned char)t->text[offset];
    /* Disk names and text RAM must never introduce host terminal controls. */
    if (ch < 0x20 || ch > 0x7e) ch = ' ';
    return (uint16_t)(ch | (t->inverse[offset] == '1' ? 0x100 : 0));
}

static void render_chunk(ssh_terminal_t *t) {
    const unsigned count = t->render_columns * t->render_rows;
    while (t->scan_cell < count) {
        unsigned row = t->scan_cell / t->render_columns;
        unsigned column = t->scan_cell % t->render_columns;
        unsigned previous_index = row * SSH_TERMINAL_COLUMNS + column;
        uint16_t cell = screen_cell(t, row, column);
        /* Some terminals scroll immediately on their final cell. Leave that
         * one cell blank without disabling the user's normal wrapping mode. */
        if (row + 1 == t->host_rows && column + 1 == t->host_columns) {
            t->previous[previous_index] = cell;
            ++t->scan_cell;
            t->positioned = false;
            continue;
        }
        if (t->previous_valid && t->previous[previous_index] == cell) {
            ++t->scan_cell;
            t->positioned = false;
            continue;
        }
        /* Worst-case cursor + SGR + one cell; never stage a partial escape. */
        if (sizeof(t->tx) - t->tx_length < 24) return;
        if (!t->positioned) {
            char cursor[24];
            int length = snprintf(cursor, sizeof(cursor), "\033[%u;%uH", row + 1, column + 1);
            append(t, cursor, (size_t)length);
            t->positioned = true;
        }
        bool inverse = !!(cell & 0x100);
        if (inverse != t->inverse_active) {
            append(t, inverse ? "\033[7m" : "\033[0m", 4);
            t->inverse_active = inverse;
        }
        t->tx[t->tx_length++] = (uint8_t)cell;
        t->frame_had_output = true;
        t->previous[previous_index] = cell;
        ++t->scan_cell;
        if (column + 1 == t->render_columns) t->positioned = false;
    }
    if (t->footer_needed) {
        static const char text_help[] = "Ctrl-] menu | BASIC: Caps Lock | Return, ~. disconnect";
        static const char graphics_help[] = "Graphics on badge | Ctrl-] menu | Return, ~. disconnect";
        const char *help = t->graphics ? graphics_help : text_help;
        size_t length = strlen(help);
        if (length > t->host_columns) length = t->host_columns;
        if (t->host_rows == 25 && length == t->host_columns) --length;
        if (sizeof(t->tx) - t->tx_length < length + 20) return;
        append(t, "\033[0m\033[25;1H\033[2K", 15);
        append(t, help, length);
        t->frame_had_output = true;
        t->inverse_active = false;
        t->footer_needed = false;
    }
    if (t->frame_had_output) {
        if (sizeof(t->tx) - t->tx_length < 24) return;
        /* The snapshot has no cursor metadata. End-of-text is a useful BASIC
         * cursor, and leaves a visible cursor without guessing RAM locations
         * that word processors and games may use for another purpose. */
        unsigned cursor_row = 1, cursor_column = 1;
        bool found = false;
        for (unsigned row = t->render_rows; row && !found; --row) {
            for (unsigned col = t->render_columns; col; --col) {
                if ((screen_cell(t, row - 1, col - 1) & 0xff) != ' ') {
                    cursor_row = row;
                    cursor_column = col < t->render_columns ? col + 1 : col;
                    found = true;
                    break;
                }
            }
        }
        char cursor[24];
        int length = snprintf(cursor, sizeof(cursor), "\033[0m\033[%u;%uH", cursor_row, cursor_column);
        append(t, cursor, (size_t)length);
        t->inverse_active = false;
    }
    t->frame_active = false;
    t->previous_valid = true;
    t->last_columns = t->columns;
    t->last_graphics = t->graphics;
}

void ssh_terminal_poll(ssh_terminal_t *t, uint32_t now) {
    if (t->active) {
        expire_sequence(t, now);
        if (deliver_pending(t) && t->sequence_literal) flush_literal_sequence(t);
    }
    size_t budget = SSH_TERMINAL_OUTPUT_BUDGET;
    while (budget) {
        if (t->tx_offset < t->tx_length) {
            if (!t->callbacks.send) return;
            size_t count = t->tx_length - t->tx_offset;
            if (count > budget) count = budget;
            size_t sent = t->callbacks.send(t->context, t->tx + t->tx_offset, count);
            /* A broken callback cannot make offsets overflow the fixed buffer. */
            if (sent > count) sent = count;
            if (!sent) return;
            t->tx_offset += sent;
            budget -= sent;
            if (t->tx_offset < t->tx_length) continue;
        }
        t->tx_length = t->tx_offset = 0;
        if (!t->active) return;
        if (!t->frame_active && !snapshot(t, now)) return;
        render_chunk(t);
        if (!t->tx_length && !t->frame_active) return;
    }
}

void ssh_terminal_close(ssh_terminal_t *t) {
    if (!t->active) return;
    t->active = t->frame_active = false;
    reset_input(t);
    t->tx_offset = 0;
    t->tx_length = sizeof(terminal_leave) - 1;
    memcpy(t->tx, terminal_leave, t->tx_length);
}

bool ssh_terminal_finished(const ssh_terminal_t *t) {
    return !t->active && t->tx_offset == t->tx_length;
}
