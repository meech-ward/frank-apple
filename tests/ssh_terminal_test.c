// SPDX-License-Identifier: MIT
#include "ssh_terminal.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* A small terminal interpreter makes the checks about what the user sees,
 * rather than asserting that the implementation emits its own escape strings. */
typedef struct {
    unsigned row, column;
    bool inverse, wrap, alternate, cursor, bracketed;
    char sequence[64];
    size_t sequence_length;
    uint16_t cell[40][100];
    unsigned max_row, max_column;
} host_terminal_t;

typedef struct {
    ssh_terminal_t terminal;
    host_terminal_t host;
    uint8_t keys[16384];
    size_t key_count, key_capacity, send_limit, send_capacity, sent, snapshots;
    unsigned columns;
    bool graphics, snapshot_failure;
    char text[SSH_TERMINAL_SCREEN_CAP], inverse[SSH_TERMINAL_SCREEN_CAP];
} fixture_t;

static fixture_t f;

static void host_character(host_terminal_t *h, uint8_t ch) {
    if (ch == 0x1b) {
        h->sequence[0] = (char)ch;
        h->sequence_length = 1;
        return;
    }
    if (ch == '\r') { h->column = 0; return; }
    if (ch == '\n') { if (h->row < 39) ++h->row; return; }
    if (h->sequence_length) {
        assert(h->sequence_length + 1 < sizeof(h->sequence));
        h->sequence[h->sequence_length++] = (char)ch;
        h->sequence[h->sequence_length] = 0;
        if (h->sequence_length == 2) { assert(ch == '['); return; }
        if (ch < 0x40 || ch > 0x7e) return;
        unsigned first = 0, second = 0;
        if (h->sequence[2] == '?') {
            sscanf(h->sequence + 3, "%u", &first);
            bool on = ch == 'h';
            if (first == 1049) h->alternate = on;
            else if (first == 25) h->cursor = on;
            else if (first == 7) h->wrap = on;
            else if (first == 2004) h->bracketed = on;
        } else {
            sscanf(h->sequence + 2, "%u;%u", &first, &second);
            switch (ch) {
                case 'H':
                    assert(first >= 1 && first <= 40 && second >= 1 && second <= 100);
                    h->row = first - 1; h->column = second - 1;
                    if (first > h->max_row) h->max_row = first;
                    if (second > h->max_column) h->max_column = second;
                    break;
                case 'J':
                    assert(first == 2);
                    for (size_t row = 0; row < 40; ++row)
                        for (size_t column = 0; column < 100; ++column) h->cell[row][column] = ' ';
                    break;
                case 'K':
                    assert(first == 2);
                    for (size_t column = 0; column < 100; ++column) h->cell[h->row][column] = ' ';
                    break;
                case 'm': assert(first == 0 || first == 7); h->inverse = first == 7; break;
                default: assert(!"Unexpected ANSI output");
            }
        }
        h->sequence_length = 0;
        return;
    }
    assert(ch >= 0x20 && ch <= 0x7e);
    assert(h->row < 40 && h->column < 100);
    h->cell[h->row][h->column] = ch | (h->inverse ? 0x100 : 0);
    ++h->column;
}

static size_t send_bytes(void *context, const uint8_t *data, size_t length) {
    fixture_t *p = context;
    if (length > p->send_limit) length = p->send_limit;
    if (length > p->send_capacity) length = p->send_capacity;
    for (size_t i = 0; i < length; ++i) host_character(&p->host, data[i]);
    p->send_capacity -= length;
    p->sent += length;
    return length;
}

static bool key(void *context, uint8_t ch) {
    fixture_t *p = context;
    if (ch != 3) {
        if (!p->key_capacity) return false;
        --p->key_capacity;
    }
    assert(p->key_count < sizeof(p->keys));
    p->keys[p->key_count++] = ch;
    return true;
}

static size_t snapshot(void *context, char *text, char *inverse, size_t capacity,
                       unsigned *columns, bool *graphics) {
    fixture_t *p = context;
    ++p->snapshots;
    if (p->snapshot_failure) return 0;
    size_t length = strlen(p->text);
    assert(length < capacity);
    memcpy(text, p->text, length + 1);
    memcpy(inverse, p->inverse, length);
    *columns = p->columns;
    *graphics = p->graphics;
    return length;
}

static void start(void) {
    memset(&f, 0, sizeof(f));
    f.host.cursor = f.host.wrap = true;
    f.key_capacity = f.send_capacity = f.send_limit = SIZE_MAX;
    f.columns = 40;
    strcpy(f.text, "]\n");
    memset(f.inverse, '0', sizeof(f.inverse));
    ssh_terminal_callbacks_t callbacks = { send_bytes, key, snapshot };
    ssh_terminal_init(&f.terminal, &callbacks, &f);
    ssh_terminal_open(&f.terminal, 0);
}

static void poll(uint32_t now) {
    size_t before = f.sent;
    ssh_terminal_poll(&f.terminal, now);
    assert(f.sent - before <= SSH_TERMINAL_OUTPUT_BUDGET);
}

static void render(uint32_t now) {
    for (unsigned i = 0; i < 100; ++i) {
        poll(now);
        if (!f.terminal.frame_active && f.terminal.tx_offset == f.terminal.tx_length) return;
    }
    assert(!"Rendering did not drain");
}

static size_t receive(const char *data, uint32_t now) {
    return ssh_terminal_receive(&f.terminal, (const uint8_t *)data, strlen(data), now);
}

static void expect_keys(const char *expected, size_t length) {
    assert(length == f.key_count);
    assert(!memcmp(f.keys, expected, length));
}

static void screen_line(unsigned row, const char *expected) {
    for (size_t i = 0; expected[i]; ++i) assert(f.host.cell[row][i] == (uint8_t)expected[i]);
}

static void keyboard_basics(void) {
    start();
    const uint8_t input[] = "PRINT 2+2\r\nCat\n\x7f\x08\t\0\xc3\xa9";
    assert(ssh_terminal_receive(&f.terminal, input, sizeof(input) - 1, 0) == sizeof(input) - 1);
    expect_keys("PRINT 2+2\rCat\r\x08\x08\t", 17);
    assert(receive("\r", 1) == 1);
    assert(receive("\n", 2) == 1);
    assert(f.keys[f.key_count - 1] == '\r');
    assert(f.key_count == 18);
    assert(receive("\033[A\n", 3) == 4);
    assert(f.keys[f.key_count - 2] == 0x0b && f.keys[f.key_count - 1] == '\r');
}

static void escape_keys(void) {
    start();
    assert(receive("\033", 0) == 1);
    poll(SSH_TERMINAL_ESCAPE_MS - 1);
    assert(!f.key_count);
    poll(SSH_TERMINAL_ESCAPE_MS);
    expect_keys("\033", 1);
    f.key_count = 0;
    const char *sequences[] = {"\033[A", "\033[B", "\033[C", "\033[D", "\033OA", "\033OB", "\033OC", "\033OD",
                               "\033[1;5A", "\033[3~", "\033[23~", "\035", "\033[99~", "\033[Z"};
    for (size_t i = 0; i < sizeof(sequences) / sizeof(*sequences); ++i) {
        for (size_t j = 0; sequences[i][j]; ++j)
            assert(ssh_terminal_receive(&f.terminal, (const uint8_t *)sequences[i] + j, 1, 200 + (uint32_t)i) == 1);
    }
    expect_keys("\x0b\x0a\x15\x08\x0b\x0a\x15\x08\x0b\x08\x1d\x1d", 12);
    assert(receive("\033x", 400) == 2);
    assert(f.keys[f.key_count - 2] == 0x1b && f.keys[f.key_count - 1] == 'x');
    size_t before = f.key_count;
    assert(receive("\033[123456789012345678901234567890~", 500) == 33);
    assert(f.key_count == before);
    assert(receive("\033[", 600) == 2);
    poll(1600);
    assert(receive("Z", 1601) == 1);
    assert(f.keys[f.key_count - 1] == 'Z');

    start();
    assert(receive("\033", UINT32_MAX - 50) == 1);
    poll(99); // unsigned wrap: 150 ms have passed
    expect_keys("\033", 1);
}

static void keyboard_backpressure(void) {
    start();
    f.key_capacity = 1;
    const char *input = "ab\ncd\r\nE\177";
    size_t length = strlen(input), used = receive(input, 0);
    assert(used == 2); // a accepted, b retained
    assert(receive(input + used, 1) == 0);
    expect_keys("a", 1);
    for (unsigned i = 0; i < 30 && (used < length || f.terminal.pending_input); ++i) {
        f.key_capacity = 1;
        poll(i + 2);
        used += receive(input + used, i + 2);
    }
    assert(used == length && !f.terminal.pending_input);
    expect_keys("ab\rcd\rE\x08", 8);

    start();
    f.key_capacity = 0;
    assert(receive("\033x", 0) == 1); // ESC retained; x still belongs to caller
    assert(receive("x", 1) == 0);
    f.key_capacity = 1; poll(2);
    expect_keys("\033", 1);
    assert(receive("x", 3) == 1); // x retained
    f.key_capacity = 1; poll(4);
    expect_keys("\033x", 2);

    start();
    f.key_capacity = 0;
    assert(receive("abc", 0) == 1);
    assert(receive("bc\003NEW\r", 1) == 4); // stop bypasses blocked 'a', N retained
    expect_keys("\003", 1);
    f.key_capacity = SIZE_MAX; poll(2);
    assert(receive("EW\r", 3) == 3);
    expect_keys("\003NEW\r", 5);
}

static void bracketed_paste(void) {
    const char *input = "\033[200~a\r\nb\033[A\033[201~";
    size_t length = strlen(input);
    for (size_t split = 0; split <= length; ++split) {
        start();
        assert(ssh_terminal_receive(&f.terminal, (const uint8_t *)input, split, 0) == split);
        assert(ssh_terminal_receive(&f.terminal, (const uint8_t *)input + split, length - split, 1) == length - split);
        expect_keys("a\rb\033[A", 6); // pasted escape text is literal, not an Up key
        assert(!f.terminal.paste && !f.terminal.sequence_length);
    }
    start();
    f.key_capacity = 0;
    assert(receive("\033[200~\033[X", 0) == 8); // mismatch starts flushing ESC, X retained by caller
    f.key_capacity = SIZE_MAX; poll(1);
    assert(receive("X\033[201~", 2) == 7);
    expect_keys("\033[X", 3);

    start();
    assert(receive("\033[200~\033[20", 0) == 10);
    poll(1000); // incomplete end marker flushes as literal pasted text
    expect_keys("\033[20", 4);

    start();
    char program[8192], expected[8192];
    size_t program_length = 0, expected_length = 0;
    memcpy(program, "\033[200~", 6); program_length = 6;
    for (unsigned line = 1; line <= 120; ++line) {
        int written = snprintf(program + program_length, sizeof(program) - program_length,
                               "%u PRINT \"PASTED LINE\"\r\n", line * 10);
        assert(written > 1 && (size_t)written < sizeof(program) - program_length);
        memcpy(expected + expected_length, program + program_length, (size_t)written - 1);
        expected_length += (size_t)written - 1;
        program_length += (size_t)written;
    }
    memcpy(program + program_length, "\033[201~", 6); program_length += 6;
    size_t used = 0;
    for (unsigned now = 0; now < 10000 && (used < program_length || f.terminal.pending_input); ++now) {
        f.key_capacity = 1;
        poll(now);
        used += ssh_terminal_receive(&f.terminal, (uint8_t *)program + used, program_length - used, now);
    }
    assert(used == program_length && !f.terminal.pending_input && !f.terminal.paste);
    expect_keys(expected, expected_length);
}

static void text_rendering(void) {
    start();
    strcpy(f.text, "]LIST\n10 PRINT \"HI\"\n]\n");
    f.inverse[0] = '1';
    f.send_limit = 3; // split every ANSI and ordinary run across sends
    render(0);
    assert(!f.host.alternate && !f.host.bracketed && f.host.cursor && f.host.wrap);
    assert(f.host.cell[0][0] == (']' | 0x100));
    screen_line(1, "10 PRINT \"HI\"");
    screen_line(2, "]");
    assert(f.host.cell[23][39] == ' ');
    size_t sent = f.sent, snapshots = f.snapshots;
    poll(99);
    assert(f.sent == sent && f.snapshots == snapshots);
    render(100);
    assert(f.sent == sent && f.snapshots == snapshots + 1);
    strcpy(f.text, "]LIST\n20 PRINT \"HI\"\n]\n");
    render(200);
    screen_line(1, "20 PRINT \"HI\"");
    assert(f.sent - sent < 30); // only the changed cell and necessary cursor/style
    f.inverse[0] = '0';
    render(300);
    assert(f.host.cell[0][0] == ']');
    strcpy(f.text, "A\033[2JB\n");
    render(400);
    screen_line(0, "A [2JB"); // snapshot text cannot inject terminal escapes

    start();
    memset(f.text, 'X', sizeof(f.text) - 1);
    f.text[sizeof(f.text) - 1] = 0;
    render(0); // a malformed/ragged callback remains bounded
    for (unsigned i = 0; i < 40; ++i) assert(f.host.cell[0][i] == 'X');
    assert(f.host.cell[1][0] == ' ');

    start();
    f.columns = 80;
    f.send_limit = 1;
    for (unsigned row = 0; row < 24; ++row) {
        for (unsigned col = 0; col < 80; ++col) {
            unsigned offset = row * 81 + col;
            f.text[offset] = 'A' + (col % 26);
            f.inverse[offset] = (col & 1) ? '0' : '1';
        }
        f.text[row * 81 + 80] = '\n';
    }
    f.text[24 * 81] = 0;
    render(0); // worst-case alternating styles across every output chunk
    for (unsigned row = 0; row < 24; ++row)
        for (unsigned col = 0; col < 80; ++col)
            assert(f.host.cell[row][col] == (row == 23 && col == 79 ? ' ' :
                   (('A' + (col % 26)) | ((col & 1) ? 0 : 0x100))));
}

static void changing_screen_size(void) {
    start();
    f.columns = 80;
    memset(f.text, '8', 80);
    strcpy(f.text + 80, "\n");
    render(0);
    assert(f.host.cell[0][79] == '8');
    f.columns = 40;
    strcpy(f.text, "Forty columns\n");
    render(100);
    screen_line(0, "Forty columns");
    for (unsigned col = 40; col < 80; ++col) assert(f.host.cell[0][col] == ' ');
    f.columns = 80;
    memset(f.text, 'A', 80);
    strcpy(f.text + 80, "\n");
    render(200);
    assert(f.host.cell[0][79] == 'A');
    ssh_terminal_resize(&f.terminal, 10, 7);
    f.host.max_column = f.host.max_row = 0;
    render(201);
    assert(f.host.max_row <= 7 && f.host.max_column <= 10);
    for (unsigned col = 0; col < 10; ++col) assert(f.host.cell[0][col] == 'A');
    for (unsigned col = 10; col < 80; ++col) assert(f.host.cell[0][col] == ' ');
    ssh_terminal_resize(&f.terminal, 80, 26);
    render(202);
    screen_line(24, "Ctrl-] menu | BASIC: Caps Lock");
    assert(f.host.cell[0][79] == 'A');
    f.graphics = true;
    render(400);
    screen_line(24, "Graphics on badge");
    ssh_terminal_resize(&f.terminal, 0, 0);
    render(401);
    assert(f.host.cell[24][0] == ' '); // footer disappears without taking an Apple row
    ssh_terminal_resize(&f.terminal, 10, 25);
    render(402);
    screen_line(24, "Graphics ");
    assert(f.host.cell[24][9] == ' '); // footer also avoids the scrolling corner
    ssh_terminal_resize(&f.terminal, 1, 1);
    render(403);
    assert(f.host.cell[0][0] == ' ');
}

static void lifecycle_and_output_pressure(void) {
    start();
    f.send_capacity = 0;
    poll(0);
    assert(!f.sent && !f.snapshots);
    assert(receive("HELLO\r", 1) == 6); // output congestion never blocks keyboard
    expect_keys("HELLO\r", 6);
    for (unsigned i = 0; i < 10000; ++i) {
        f.send_capacity = 1;
        poll(1);
        if (!f.terminal.frame_active && f.terminal.tx_offset == f.terminal.tx_length) break;
    }
    assert(!f.terminal.frame_active);
    assert(f.host.cell[0][0] == ']');
    f.snapshot_failure = true;
    size_t sent = f.sent;
    render(100);
    assert(f.sent == sent && f.host.cell[0][0] == ']');
    f.snapshot_failure = false;
    f.send_capacity = 0;
    ssh_terminal_close(&f.terminal);
    assert(!ssh_terminal_finished(&f.terminal));
    assert(!receive("BAD", 200));
    poll(200);
    assert(!ssh_terminal_finished(&f.terminal));
    f.send_capacity = SIZE_MAX;
    poll(201);
    assert(ssh_terminal_finished(&f.terminal));
    assert(!f.host.alternate && !f.host.bracketed && f.host.cursor && f.host.wrap && !f.host.inverse);
    ssh_terminal_close(&f.terminal);
    assert(ssh_terminal_finished(&f.terminal));
    strcpy(f.text, "NEW SESSION\n");
    ssh_terminal_open(&f.terminal, 300);
    render(300);
    screen_line(0, "NEW SESSION");
    f.key_capacity = 0;
    assert(receive("X", 301) == 1);
    assert(receive("\033[", 302) == 0);
    ssh_terminal_open(&f.terminal, 303); // lost connection: discard unfinished input
    f.key_capacity = SIZE_MAX;
    poll(303);
    assert(receive("A", 304) == 1);
    assert(f.keys[f.key_count - 1] == 'A');
    assert(f.key_count == 7);

    start();
    f.send_capacity = 3; // close while the opening ANSI sequence is split
    poll(0);
    assert(f.host.sequence_length == 3);
    ssh_terminal_close(&f.terminal);
    f.send_capacity = SIZE_MAX;
    poll(1);
    assert(ssh_terminal_finished(&f.terminal));
    assert(!f.host.alternate && !f.host.bracketed && f.host.cursor && f.host.wrap);
}

static void input_bounds(void) {
    start();
    uint8_t data[1024];
    memset(data, 'Q', sizeof(data));
    assert(ssh_terminal_receive(&f.terminal, data, sizeof(data), 0) == SSH_TERMINAL_INPUT_BUDGET);
    assert(f.key_count == SSH_TERMINAL_INPUT_BUDGET);
    start();
    unsigned random = 1234;
    for (unsigned i = 0; i < 10000; ++i) {
        random = random * 1664525U + 1013904223U;
        uint8_t ch = (uint8_t)(random >> 24);
        assert(ssh_terminal_receive(&f.terminal, &ch, 1, i) <= 1);
        if (!(i & 63)) poll(i);
        assert(f.terminal.sequence_length <= sizeof(f.terminal.sequence));
    }
}

int main(void) {
    keyboard_basics();
    escape_keys();
    keyboard_backpressure();
    bracketed_paste();
    text_rendering();
    changing_screen_size();
    lifecycle_and_output_pressure();
    input_bounds();
    printf("SSH terminal: keyboard, paste/backpressure, ANSI mirror, resize and lifecycle checks passed (%zu-byte state).\n", sizeof(ssh_terminal_t));
    return 0;
}
