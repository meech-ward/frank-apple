/* SPDX-License-Identifier: MIT */
#include "ssh_control.h"
#include "ssh_identity.h"
#include "ssh_terminal.h"
#include "ssh_transport.h"
#include "typing.h"
#include "disk_ui.h"
#include "netcard.h"
#include "debug_log.h"
#include "pico/rand.h"
#include "pico/time.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <stdio.h>
#include <string.h>

static const wifi_config *settings;
static ssh_terminal_t terminal;
static bool requested, prepared, attempted, network_up, was_connected;
static bool stopping;
static uint32_t stopping_at, seen_session;
static bool input_finished, session_closing;
static uint32_t input_finished_at, session_close_at;
static bool listen_failed;
static uint32_t listen_retry_at;
static const char *setup_error;
static uint8_t input[256];
static size_t input_size, input_offset;

static bool configured(void) {
    if (!settings) return false;
    size_t size = 0;
    while (size < sizeof(settings->ssh_password) && settings->ssh_password[size]) ++size;
    return size >= 8 && size <= 64;
}

static bool random_seed(uint8_t *out, size_t size) {
    /* Pico SDK mixes RP2350 hardware TRNG entropy into every get_rand_64().
     * This is also the SDK's mbedTLS hardware-entropy source. */
    while (size) {
        uint64_t value = get_rand_64();
        size_t n = size < sizeof(value) ? size : sizeof(value);
        memcpy(out, &value, n); out += n; size -= n;
    }
    return true;
}

static size_t terminal_send(void *context, const uint8_t *data, size_t length) {
    (void)context;
    return ssh_transport_write(data, length);
}
static bool terminal_key(void *context, uint8_t key) {
    (void)context;
    if (!requested) return false;
    return remote_control_try_key(key);
}
static size_t terminal_snapshot(void *context, char *text, char *inverse,
        size_t capacity, unsigned *columns, bool *graphics) {
    (void)context;
    *columns = remote_control_columns();
    *graphics = !disk_ui_is_visible() && remote_control_graphics();
    return remote_control_screen(text, inverse, capacity);
}

void ssh_control_init(const wifi_config *config) {
    settings = config;
    requested = configured() && settings->ssh_enabled;
    prepared = attempted = network_up = was_connected = false;
    stopping = false; seen_session = 0;
    input_finished = session_closing = false;
    listen_failed = false; listen_retry_at = 0;
    setup_error = NULL;
    input_size = input_offset = 0;
    const ssh_terminal_callbacks_t callbacks = {
        .send = terminal_send, .key = terminal_key, .snapshot = terminal_snapshot
    };
    ssh_terminal_init(&terminal, &callbacks, NULL);
}

bool ssh_control_enabled(void) { return requested; }

void ssh_control_toggle(void) {
    if (!configured()) return;
    requested = !requested;
    if (requested) { attempted = false; setup_error = NULL; listen_failed = false; }
    /* State changes take effect during the next core-0 poll. In particular,
     * toggling from the SSH-controlled menu must not recursively destroy the
     * transport while its receive bytes are still being processed. */
}

static bool prepare(void) {
    uint8_t seed[32];
    attempted = true;
    if (!ssh_identity_load(seed, random_seed)) {
        setup_error = ssh_identity_error();
        return false;
    }
    prepared = ssh_transport_init(seed, settings->ssh_password);
    memset(seed, 0, sizeof(seed));
    if (!prepared) setup_error = ssh_transport_status();
    else MII_DEBUG_PRINTF("SSH: host key %s\n", ssh_transport_fingerprint());
    return prepared;
}

void ssh_control_poll(bool network_ready) {
    network_up = network_ready;
    uint32_t now = (uint32_t)(time_us_64() / 1000u);
    if (requested && network_ready && !prepared && !attempted) prepare();
    if (!prepared) return;
    bool running = requested && network_ready;
    if ((!running || stopping) && ssh_transport_enabled()) {
        if (network_ready && ssh_transport_connected() && !stopping) {
            ssh_terminal_close(&terminal);
            stopping = true; stopping_at = now;
        }
        if (stopping && network_ready && (uint32_t)(now - stopping_at) < 500u) {
            ssh_terminal_poll(&terminal, now);
            if (ssh_terminal_finished(&terminal)) ssh_transport_disconnect();
            ssh_transport_poll();
            return; /* Let the terminal cleanup and SSH close reach the peer. */
        }
        ssh_transport_set_enabled(false);
        stopping = false; was_connected = false; input_size = input_offset = 0;
        return;
    }
    if (running && !ssh_transport_enabled() &&
            (!listen_failed || (int32_t)(now - listen_retry_at) >= 0)) {
        listen_failed = !ssh_transport_set_enabled(true);
        listen_retry_at = now + 2000u;
    }
    ssh_transport_poll();
    bool connected = ssh_transport_connected();
    uint32_t session = ssh_transport_session_id();
    if (connected && (!was_connected || seen_session != session)) {
        input_size = input_offset = 0;
        ssh_terminal_open(&terminal, now);
        seen_session = session;
        input_finished = session_closing = false;
    }
    if (!connected) {
        input_size = input_offset = 0;
        was_connected = false;
        return; /* A later connection starts with a fresh screen and parser. */
    }
    was_connected = true;
    unsigned columns, rows;
    ssh_transport_terminal_size(&columns, &rows);
    ssh_terminal_resize(&terminal, columns, rows);
    if (session_closing) {
        ssh_terminal_poll(&terminal, now);
        if (ssh_terminal_finished(&terminal) || (uint32_t)(now - session_close_at) >= 500u)
            ssh_transport_disconnect();
        return;
    }
    if (ssh_transport_take_interrupt()) {
        const uint8_t stop = 3;
        input_size = input_offset = 0;
        ssh_terminal_receive(&terminal, &stop, 1, now);
    }
    /* Bounded work per emulator iteration. Keep any unaccepted bytes until
     * the Apple keyboard latch/FIFO can accept them; never drop a paste. */
    if (input_offset == input_size) {
        input_offset = 0;
        input_size = ssh_transport_read(input, sizeof(input));
    }
    if (input_offset < input_size)
        input_offset += ssh_terminal_receive(&terminal, input + input_offset,
                                             input_size - input_offset, now);
    ssh_terminal_poll(&terminal, now);
    /* A redirected stdin EOF must not discard the final retained/pasted keys.
     * Allow one last screen refresh after the keyboard FIFO drains, then end
     * this channel while keeping the listener and the Apple II running. */
    if (ssh_transport_input_ended() && input_offset == input_size &&
            !terminal.pending_input && !terminal.sequence_length && !typing_pending()) {
        if (!input_finished) { input_finished = true; input_finished_at = now; }
        if ((uint32_t)(now - input_finished_at) >= 250u) {
            session_closing = true;
            session_close_at = now;
            ssh_terminal_close(&terminal);
        }
    } else input_finished = false;
}

const char *ssh_control_label(void) {
    if (!configured()) return "SSH: add password to wifi.ini";
    if (!requested) return "SSH: OFF";
    if (setup_error) return "SSH: setup failed";
    if (!network_up) return "SSH: waiting for WiFi";
    if (listen_failed) return "SSH: listener unavailable";
    if (prepared && ssh_transport_connected()) return "SSH: connected";
    if (prepared && ssh_transport_listening()) return "SSH: ON";
    return "SSH: starting";
}

void ssh_control_address(char *out, size_t capacity) {
    if (!capacity) return;
    if (!configured())
        snprintf(out, capacity, "Set [ssh] password in /wifi.ini");
    else if (setup_error) snprintf(out, capacity, "%s", setup_error);
    else if (listen_failed && requested) snprintf(out, capacity, "%s", ssh_transport_status());
    else if (!network_up || !netif_default || ip4_addr_isany_val(*netif_ip4_addr(netif_default))) {
        const char *status = netcard_wifi_status();
        snprintf(out, capacity, "%s", status ? status : "Waiting for an IP address");
    } else snprintf(out, capacity, "ssh apple@%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
}
