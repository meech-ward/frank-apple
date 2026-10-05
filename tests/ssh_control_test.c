// SPDX-License-Identifier: MIT
#include "ssh_control_test_platform.h"
#include "ssh_control.h"
#include "ssh_identity.h"
#include "ssh_transport.h"
#include "typing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static wifi_config config;
static bool identity_ok, transport_ok, listen_ok, enabled, connected, graphics, menu, pending_interrupt, input_eof;
static bool disable_on_key;
static unsigned identity_calls, init_calls, enable_calls, disconnect_calls, random_calls;
static uint32_t generation;
static uint64_t now_ms;
static size_t key_capacity, key_count, typing_backlog, received_length, received_offset, sent_length, send_capacity;
static uint8_t keys[16384], received[16384], sent[32768];
static unsigned host_columns, host_rows;
static struct netif interface;
struct netif *netif_default = &interface;

uint64_t get_rand_64(void) { ++random_calls; return UINT64_C(0x239f662e88a811df); }
uint64_t time_us_64(void) { return now_ms * 1000; }
const char *netcard_wifi_status(void) { return "WiFi disconnected"; }
const char *ip4addr_ntoa(const ip4_addr_t *address) { (void)address; return "192.168.4.1"; }
bool disk_ui_is_visible(void) { return menu; }
bool remote_control_graphics(void) { return graphics; }
unsigned remote_control_columns(void) { return 40; }
size_t remote_control_screen(char *text, char *inverse, size_t capacity) {
    const char *screen = menu ? "BADGE MENU\n" : "]PRINT 2+2\n4\n]\n";
    size_t length = strlen(screen);
    assert(length < capacity);
    memcpy(text, screen, length + 1);
    memset(inverse, '0', length);
    return length;
}
bool remote_control_try_key(uint8_t key) {
    if (key != 3 && key != 0x1d) {
        if (!key_capacity) return false;
        --key_capacity;
    }
    assert(key_count < sizeof(keys));
    keys[key_count++] = key;
    if (key == 3 || key == 0x1d) typing_backlog = 0;
    else ++typing_backlog;
    if (disable_on_key && key == '%') ssh_control_toggle();
    return true;
}
size_t typing_pending(void) { return typing_backlog; }

bool ssh_identity_load(uint8_t seed[32], bool (*random_bytes)(uint8_t *, size_t)) {
    ++identity_calls;
    if (!identity_ok) return false;
    return random_bytes(seed, 32);
}
const char *ssh_identity_error(void) { return "Host key storage unavailable"; }
bool ssh_transport_init(const uint8_t seed[32], const char *password) {
    ++init_calls;
    assert(seed[0] == 0xdf);
    assert(!strcmp(password, "test-ssh-password"));
    return transport_ok;
}
bool ssh_transport_set_enabled(bool value) {
    ++enable_calls;
    if (value && !listen_ok) { enabled = false; return false; }
    enabled = value;
    if (!value) connected = false;
    return true;
}
bool ssh_transport_enabled(void) { return enabled; }
bool ssh_transport_listening(void) { return enabled; }
void ssh_transport_poll(void) {}
bool ssh_transport_connected(void) { return enabled && connected; }
uint32_t ssh_transport_session_id(void) { return generation; }
bool ssh_transport_input_ended(void) { return input_eof && received_offset == received_length; }
bool ssh_transport_take_interrupt(void) {
    bool value = pending_interrupt; pending_interrupt = false; return value;
}
void ssh_transport_disconnect(void) { ++disconnect_calls; connected = false; }
size_t ssh_transport_read(uint8_t *data, size_t capacity) {
    size_t length = received_length - received_offset;
    if (length > capacity) length = capacity;
    memcpy(data, received + received_offset, length);
    received_offset += length;
    return length;
}
size_t ssh_transport_write(const uint8_t *data, size_t length) {
    if (length > send_capacity) length = send_capacity;
    assert(sent_length + length < sizeof(sent));
    memcpy(sent + sent_length, data, length);
    sent_length += length;
    sent[sent_length] = 0;
    send_capacity -= length;
    return length;
}
void ssh_transport_terminal_size(unsigned *columns, unsigned *rows) {
    *columns = host_columns; *rows = host_rows;
}
const char *ssh_transport_status(void) { return "SSH crypto setup failed"; }
const char *ssh_transport_fingerprint(void) { return "SHA256:TEST_PUBLIC_KEY"; }

static void reset(bool configured_password, bool autostart) {
    memset(&config, 0, sizeof(config));
    if (configured_password) strcpy(config.ssh_password, "test-ssh-password");
    config.ssh_enabled = autostart;
    identity_ok = transport_ok = listen_ok = true;
    enabled = connected = graphics = menu = disable_on_key = pending_interrupt = input_eof = false;
    identity_calls = init_calls = enable_calls = disconnect_calls = random_calls = 0;
    generation = 0; now_ms = 0;
    key_capacity = send_capacity = SIZE_MAX;
    key_count = typing_backlog = received_length = received_offset = sent_length = 0;
    sent[0] = 0;
    host_columns = 80; host_rows = 26;
    interface.ip.address = 1;
    netif_default = &interface;
    ssh_control_init(&config);
}
static void poll(bool network) { ssh_control_poll(network); now_ms += 10; }
static void poll_many(unsigned count, bool network) { while (count--) poll(network); }
static void connect_session(const char *input) {
    connected = true; ++generation;
    received_offset = 0; received_length = strlen(input);
    assert(received_length < sizeof(received));
    memcpy(received, input, received_length);
}
static void assert_keys(const char *expected) {
    assert(key_count == strlen(expected));
    assert(!memcmp(keys, expected, key_count));
}

static void activation_policy(void) {
    reset(false, false);
    poll_many(10, true);
    ssh_control_toggle(); poll(true);
    assert(!ssh_control_enabled() && !enabled && !identity_calls && !init_calls);
    assert(strstr(ssh_control_label(), "password"));

    /* Defense in depth even if a caller passes an invalid config directly. */
    reset(false, true);
    poll_many(10, true);
    assert(!enabled && !identity_calls && !init_calls);

    reset(true, false);
    poll_many(10, true);
    assert(!identity_calls && !init_calls && !enabled);
    ssh_control_toggle();
    poll(false);
    assert(ssh_control_enabled() && !identity_calls && !enabled);
    assert(strstr(ssh_control_label(), "waiting"));
    poll(true);
    assert(enabled && identity_calls == 1 && init_calls == 1 && random_calls == 4);
    poll_many(10, true);
    assert(identity_calls == 1 && init_calls == 1);
    assert(!strcmp(ssh_control_label(), "SSH: ON"));
    char address[128];
    ssh_control_address(address, sizeof(address));
    assert(!strcmp(address, "ssh apple@192.168.4.1"));
    netif_default = NULL;
    ssh_control_address(address, sizeof(address));
    assert(!strcmp(address, "WiFi disconnected"));
    ssh_control_toggle(); poll_many(100, true);
    assert(!enabled && !ssh_control_enabled());
    ssh_control_toggle(); poll(true);
    assert(enabled && identity_calls == 1 && init_calls == 1);
}

static void setup_failures(void) {
    reset(true, true);
    identity_ok = false;
    poll_many(30, true);
    assert(identity_calls == 1 && !init_calls && !enabled);
    assert(strstr(ssh_control_label(), "failed"));
    char address[128]; ssh_control_address(address, sizeof(address));
    assert(!strcmp(address, "Host key storage unavailable"));
    identity_ok = true;
    ssh_control_toggle(); poll(true);
    ssh_control_toggle(); poll(true);
    assert(identity_calls == 2 && init_calls == 1 && enabled);

    reset(true, true);
    transport_ok = false;
    poll_many(30, true);
    assert(identity_calls == 1 && init_calls == 1 && !enabled);
    assert(strstr(ssh_control_label(), "failed"));

    reset(true, true);
    listen_ok = false;
    poll(true);
    assert(!enabled && enable_calls == 1 && identity_calls == 1 && init_calls == 1);
    poll_many(100, true);
    assert(!enabled && enable_calls == 1); // no tight retry loop when lwIP is short of memory
    listen_ok = true;
    poll_many(120, true);
    assert(enabled && enable_calls == 2 && identity_calls == 1 && init_calls == 1);

    reset(true, true);
    poll(false); assert(!identity_calls);
    poll(true); assert(enabled);
    connect_session(""); poll(true);
    assert(!strcmp(ssh_control_label(), "SSH: connected"));
    poll(false); assert(!enabled);
    poll(true); assert(enabled && identity_calls == 1 && init_calls == 1);
}

static void full_keyboard_fifo(void) {
    reset(true, true); poll(true);
    char program[4096], expected[4096];
    size_t n = 0, e = 0;
    for (unsigned line = 1; line <= 100; ++line) {
        int length = snprintf(program + n, sizeof(program) - n, "%u PRINT \"HELLO\"\r\n", line * 10);
        assert(length > 1 && (size_t)length < sizeof(program) - n);
        memcpy(expected + e, program + n, (size_t)length - 1);
        n += (size_t)length; e += (size_t)length - 1;
    }
    expected[e] = 0;
    connect_session(program);
    for (unsigned i = 0; i < 10000 && key_count < e; ++i) {
        key_capacity = 1;
        poll(true);
    }
    assert_keys(expected);
    assert(strstr((const char *)sent, "PRINT 2+2"));
    assert(strstr((const char *)sent, "BASIC: Caps Lock"));
}

static void reconnect_isolation(void) {
    reset(true, true); poll(true);
    key_capacity = 0;
    connect_session("OLD\033["); poll(true);
    assert(!key_count);
    connected = false; poll(true);
    connect_session("NEW\r"); key_capacity = SIZE_MAX;
    poll_many(10, true);
    assert_keys("NEW\r");

    reset(true, true); poll(true);
    key_capacity = 0;
    connect_session("OLD\033["); poll(true);
    assert(!key_count);
    /* A complete disconnect/reconnect can occur between application polls. */
    connect_session("NEW\r"); key_capacity = SIZE_MAX;
    poll_many(10, true);
    assert_keys("NEW\r");

    reset(true, true); poll(true);
    key_capacity = 0;
    connect_session("OLD\033["); poll(true);
    assert(!key_count);
    pending_interrupt = true;
    received_offset = 0; received_length = 4; memcpy(received, "NEW\r", 4);
    poll(true);
    assert_keys("\003");
    key_capacity = SIZE_MAX; poll_many(10, true);
    assert_keys("\003NEW\r");
}

static void graceful_disable(void) {
    reset(true, true); poll(true);
    connect_session(""); poll_many(10, true);
    ssh_control_toggle(); poll_many(100, true);
    assert(!enabled && !ssh_control_enabled());
    assert(strstr((const char *)sent, "\033[0m\r\n"));

    reset(true, true); poll(true);
    connect_session(""); poll_many(10, true);
    send_capacity = 0;
    ssh_control_toggle(); poll_many(200, true);
    assert(!enabled); // a peer that cannot read cannot prevent menu shutdown
}

static void eof_waits_for_typed_program(void) {
    reset(true, true); poll(true);
    key_capacity = 0;
    connect_session("END\r"); input_eof = true;
    poll_many(100, true);
    assert(connected && !key_count && !disconnect_calls);
    key_capacity = SIZE_MAX;
    poll_many(10, true);
    assert_keys("END\r");
    assert(connected && typing_backlog == 4 && !disconnect_calls);
    poll_many(100, true);
    assert(connected && !disconnect_calls); // Apple has not consumed its queue yet
    while (typing_backlog) { --typing_backlog; poll(true); }
    poll_many(20, true);
    assert(connected && !disconnect_calls); // leave time to mirror the final screen
    poll_many(50, true);
    assert(!connected && disconnect_calls && enabled); // SSH listener remains reusable
    assert(strstr((const char *)sent, "\033[0m\r\n"));

    reset(true, true); poll(true);
    connect_session("\033"); input_eof = true;
    poll(true);
    assert(connected && !key_count && !disconnect_calls);
    poll_many(20, true);
    assert_keys("\033"); // isolated Escape is delivered before EOF closes the session
    assert(connected && typing_backlog == 1);
    typing_backlog = 0;
    poll_many(50, true);
    assert(!connected && disconnect_calls);

    reset(true, true); poll(true);
    connect_session(""); input_eof = true;
    send_capacity = 0;
    poll_many(120, true);
    assert(!connected && disconnect_calls && enabled); // EOF peer with no receive window cannot hang cleanup
}

int main(void) {
    activation_policy();
    setup_failures();
    full_keyboard_fifo();
    reconnect_isolation();
    graceful_disable();
    eof_waits_for_typed_program();
    puts("SSH control: activation/storage policy, typing backpressure, reconnect isolation, EOF drain and graceful disable passed.");
    return 0;
}
