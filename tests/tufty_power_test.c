// SPDX-License-Identifier: MIT
#include "test-tufty-power-platform.h"
#include "tufty_power.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static test_powman pm;
static test_usb usb;
test_powman *powman_hw = &pm;
test_usb *usb_hw = &usb;
static struct { bool output, input, up, down; unsigned function; } pins[48];
static uint32_t elapsed, time_base, release_at, leds, led_states;
static bool watchdog_boot, interrupts_off, wakeups_disabled, timer_stopped;
static bool debug_ignored, configured, requested, rebooted, accept_config, accept_state;
static unsigned init_count;
static jmp_buf power_exit;

uint32_t save_and_disable_interrupts(void) { interrupts_off = true; return 0; }
void set_sys_clock_48mhz(void) { assert(interrupts_off); }
void gpio_init(unsigned pin) { assert(pin < 48); ++init_count; }
void gpio_init_mask(uint32_t mask) { assert(mask == 15); ++init_count; }
void gpio_set_dir(unsigned pin, bool out) { pins[pin].output = out; }
void gpio_set_dir_out_masked(uint32_t mask) { assert(mask == 15); }
void gpio_set_input_enabled(unsigned pin, bool enabled) { pins[pin].input = enabled; }
void gpio_set_function(unsigned pin, unsigned function) { pins[pin].function = function; }
void gpio_set_pulls(unsigned pin, bool up, bool down) { pins[pin].up = up; pins[pin].down = down; }
void gpio_disable_pulls(unsigned pin) { gpio_set_pulls(pin, false, false); }
void gpio_pull_up(unsigned pin) { gpio_set_pulls(pin, true, false); }
bool gpio_get(unsigned pin) { assert(pin == BW_RESET_SW); return elapsed >= release_at; }
void gpio_put_masked(uint32_t mask, uint32_t value) {
    assert(mask == 15 && value <= 15); leds = value; led_states |= 1u << value;
}
void hw_set_bits(uint32_t *reg, uint32_t bits) { *reg |= bits; }
void reset_block_mask(uint32_t mask) { assert(mask == RESETS_RESET_USBCTRL_BITS); }
void unreset_block_mask_wait_blocking(uint32_t mask) { assert(mask == RESETS_RESET_USBCTRL_BITS); }
void powman_disable_all_wakeups(void) { wakeups_disabled = true; }
void powman_timer_stop(void) { timer_stopped = true; }
void powman_set_debug_power_request_ignored(bool ignored) { debug_ignored = ignored; }
bool powman_configure_wakeup_state(powman_power_state off, powman_power_state on) {
    assert(off == 0 && on == ((1u << 3) | (1u << 2)));
    assert(interrupts_off && wakeups_disabled && timer_stopped && debug_ignored);
    configured = true; return accept_config;
}
int powman_set_power_state(powman_power_state state) {
    assert(configured && state == 0);
    for (unsigned i = 0; i < 4; ++i) assert(pm.boot[i] == 0);
    requested = true; return accept_state ? PICO_OK : -1;
}
bool watchdog_caused_reboot(void) { return watchdog_boot; }
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    assert(pc == 0 && sp == 0 && delay_ms == 10); rebooted = true;
}
uint32_t time_us_32(void) { return time_base + elapsed; }
void sleep_ms(uint32_t ms) { assert(ms == 5); elapsed += ms * 1000; assert(elapsed <= 2000000); }
void __wfi(void) { assert(requested || rebooted); longjmp(power_exit, 1); }

static void reset_fixture(void) {
    memset(&pm, 0, sizeof(pm)); memset(&usb, 0, sizeof(usb));
    memset(pins, 0xff, sizeof(pins));
    for (unsigned i = 0; i < 4; ++i) pm.boot[i] = 0xffffffff;
    pm.chip_reset = POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS;
    elapsed = time_base = leds = led_states = init_count = 0;
    release_at = 3000000;
    watchdog_boot = interrupts_off = wakeups_disabled = timer_stopped = false;
    debug_ignored = configured = requested = rebooted = false;
    accept_config = accept_state = true;
}
static void run(void) { if (!setjmp(power_exit)) tufty_power_boot_check(); }

int main(void) {
    reset_fixture(); pm.chip_reset = 0; run(); // cold boot even with switch held
    assert(!init_count && !elapsed && !configured);
    reset_fixture(); watchdog_boot = true; run();
    assert(!init_count && !elapsed && !configured);
    const uint32_t releases[] = {0, 5000, 500000, 1995000, 2000000};
    for (unsigned i = 0; i < sizeof(releases)/sizeof(releases[0]); ++i) {
        reset_fixture(); release_at = releases[i]; run();
        assert(!configured && !interrupts_off && !rebooted && !leds);
        assert(elapsed == release_at);
    }
    for (unsigned wrap = 0; wrap < 2; ++wrap) {
        reset_fixture(); time_base = wrap ? UINT32_MAX - 10000 : 0; run();
        assert(requested && !rebooted && elapsed == 2000000 && !leds);
        assert((led_states & ((1u << 1) | (1u << 3) | (1u << 7) | (1u << 15))) ==
               ((1u << 1) | (1u << 3) | (1u << 7) | (1u << 15)));
        for (unsigned i = 0; i < 48; ++i) assert(!pins[i].output && !pins[i].input);
        assert(pins[BW_PSRAM_CS].up && !pins[BW_PSRAM_CS].down);
        assert(!pins[BW_SW_POWER_EN].up && !pins[BW_SW_POWER_EN].down);
        assert(pins[23].down && pins[26].down); // Wi-Fi enable and LCD backlight
        assert(!usb.inte && (usb.phy_direct & USB_USBPHY_DIRECT_TX_PD_BITS));
        assert(usb.phy_direct & USB_USBPHY_DIRECT_RX_PD_BITS);
    }
    reset_fixture(); accept_config = false; run();
    assert(rebooted && !requested);
    reset_fixture(); accept_state = false; run();
    assert(rebooted && requested);
    puts("PASS: cold/watchdog/short/long RESET, release boundary, timer wrap, LED feedback, peripheral parking, wake state and failure recovery");
}
