// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "register-bits.h"
#define pico_board_cmake_set(...)
#define pico_board_cmake_set_default(...)
#include "pimoroni_tufty2350.h"
#define __no_inline_not_in_flash_func(name) name
#define __wfi test_wait_for_interrupt
#define NUM_BANK0_GPIOS 48
#define GPIO_IN 0
#define GPIO_FUNC_SIO 5
#define PICO_OK 0
#define POWMAN_POWER_STATE_NONE 0
#define POWMAN_POWER_DOMAIN_SWITCHED_CORE 3
#define POWMAN_POWER_DOMAIN_XIP_CACHE 2
typedef uint32_t powman_power_state;
typedef struct { uint32_t chip_reset, vreg_ctrl, boot[4]; } test_powman;
typedef struct { uint32_t muxing, main_ctrl, sie_ctrl, inte, phy_direct, phy_direct_override; } test_usb;
extern test_powman *powman_hw;
extern test_usb *usb_hw;
uint32_t save_and_disable_interrupts(void);
void set_sys_clock_48mhz(void);
void gpio_init(unsigned pin);
void gpio_init_mask(uint32_t mask);
void gpio_set_dir(unsigned pin, bool out);
void gpio_set_dir_out_masked(uint32_t mask);
void gpio_set_input_enabled(unsigned pin, bool enabled);
void gpio_set_function(unsigned pin, unsigned function);
void gpio_set_pulls(unsigned pin, bool up, bool down);
void gpio_disable_pulls(unsigned pin);
void gpio_pull_up(unsigned pin);
bool gpio_get(unsigned pin);
void gpio_put_masked(uint32_t mask, uint32_t value);
void hw_set_bits(uint32_t *reg, uint32_t bits);
void reset_block_mask(uint32_t mask);
void unreset_block_mask_wait_blocking(uint32_t mask);
void powman_disable_all_wakeups(void);
void powman_timer_stop(void);
void powman_set_debug_power_request_ignored(bool ignored);
bool powman_configure_wakeup_state(powman_power_state off, powman_power_state on);
int powman_set_power_state(powman_power_state state);
static inline powman_power_state powman_power_state_with_domain_on(powman_power_state state, unsigned domain) {
    return state | (1u << domain);
}
bool watchdog_caused_reboot(void);
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms);
uint32_t time_us_32(void);
void sleep_ms(uint32_t ms);
void __wfi(void);
