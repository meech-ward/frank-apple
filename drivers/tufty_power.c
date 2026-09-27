// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Pimoroni Ltd
// Copyright (c) 2026 FRANK Apple downstream contributors
// Low-power GPIO/USB/POWMAN sequence adapted from Pimoroni badgeware-cpp:
// https://github.com/pimoroni/badgeware-cpp/blob/7d1ef0882c6e9dcf374542528f6f211e287b5afb/lib/powman.c
// See licenses/Pimoroni-Tufty-MIT.txt and docs/POWER.md for changes and limits.

#include "tufty_power.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/powman.h"
#include "hardware/resets.h"
#include "hardware/structs/usb.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#define HOLD_US 2000000u
#define LED_MASK 0x0fu

// This path is boot-only: PSRAM is not mapped, core 1 is still in the boot ROM,
// and no USB, display DMA, Wi-Fi, filesystem or emulator state is live.
static void __no_inline_not_in_flash_func(tufty_power_off)(void) {
    save_and_disable_interrupts();
    set_sys_clock_48mhz();

    for (unsigned pin = 0; pin < NUM_BANK0_GPIOS; ++pin) {
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_IN);
        gpio_set_input_enabled(pin, false);
        switch (pin) {
            case BW_PSRAM_CS:
                gpio_set_pulls(pin, true, false);
                break;
            case BW_RESET_SW:
            case BW_SWITCH_HOME:
            case BW_SW_POWER_EN:
            case 40: // battery sense
            case 42:
                gpio_disable_pulls(pin);
                break;
            case BW_SWITCH_A:
            case BW_SWITCH_B:
            case BW_SWITCH_C:
            case BW_SWITCH_UP:
            case BW_SWITCH_DOWN:
                gpio_set_pulls(pin, true, false);
                break;
            default:
                gpio_set_pulls(pin, false, true);
                break;
        }
    }

    hw_set_bits(&powman_hw->vreg_ctrl,
                POWMAN_PASSWORD_BITS | POWMAN_VREG_CTRL_UNLOCK_BITS);

    // The PHY override only takes effect with USBCTRL out of reset. This is
    // necessary even though neither TinyUSB device nor host has started yet.
    reset_block_mask(RESETS_RESET_USBCTRL_BITS);
    unreset_block_mask_wait_blocking(RESETS_RESET_USBCTRL_BITS);
    usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS | USB_USB_MUXING_SOFTCON_BITS;
    usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BITS;
    usb_hw->sie_ctrl = USB_SIE_CTRL_EP0_INT_1BUF_BITS;
    usb_hw->inte = 0; // interrupts stay disabled throughout shutdown
    usb_hw->phy_direct = USB_USBPHY_DIRECT_TX_PD_BITS | USB_USBPHY_DIRECT_RX_PD_BITS |
        USB_USBPHY_DIRECT_DM_PULLDN_EN_BITS | USB_USBPHY_DIRECT_DP_PULLDN_EN_BITS;
    usb_hw->phy_direct_override =
        USB_USBPHY_DIRECT_RX_DM_BITS | USB_USBPHY_DIRECT_RX_DP_BITS | USB_USBPHY_DIRECT_RX_DD_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DIFFMODE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_FSSLEW_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_PD_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_RX_PD_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DM_PULLDN_EN_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_DP_PULLDN_EN_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_EN_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_HISEL_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_HISEL_OVERRIDE_EN_BITS;

    // Shipping-style off: only the physical RESET button powers the badge on.
    // Do not inherit a wake timer or button/RTC wake source from other firmware.
    powman_disable_all_wakeups();
    powman_timer_stop();
    powman_set_debug_power_request_ignored(true);
    powman_power_state wake = powman_power_state_with_domain_on(
        POWMAN_POWER_STATE_NONE, POWMAN_POWER_DOMAIN_SWITCHED_CORE);
    wake = powman_power_state_with_domain_on(wake, POWMAN_POWER_DOMAIN_XIP_CACHE);
    if (powman_configure_wakeup_state(POWMAN_POWER_STATE_NONE, wake)) {
        // Wake through the boot ROM, never resume a discarded Apple session.
        for (unsigned i = 0; i < 4; ++i) powman_hw->boot[i] = 0;
        if (powman_set_power_state(POWMAN_POWER_STATE_NONE) == PICO_OK) {
            while (true) __wfi();
        }
    }
    // A rejected power transition must not leave a dark but running badge.
    // Restart normally; the watchdog boot bypasses the held-button check.
    watchdog_reboot(0, 0, 10);
    while (true) __wfi();
}

void tufty_power_boot_check(void) {
    if (!(powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) ||
        watchdog_caused_reboot()) return;

    gpio_init(BW_RESET_SW);
    gpio_set_dir(BW_RESET_SW, GPIO_IN);
    gpio_pull_up(BW_RESET_SW);
    if (gpio_get(BW_RESET_SW)) return;

    gpio_init_mask(LED_MASK);
    gpio_put_masked(LED_MASK, 0);
    gpio_set_dir_out_masked(LED_MASK);
    uint32_t start = time_us_32();
    while (!gpio_get(BW_RESET_SW)) {
        uint32_t elapsed = time_us_32() - start;
        if (elapsed >= HOLD_US) {
            gpio_put_masked(LED_MASK, 0);
            tufty_power_off();
        }
        unsigned count = 1u + elapsed / (HOLD_US / 4u);
        gpio_put_masked(LED_MASK, (1u << count) - 1u);
        sleep_ms(5);
    }
    gpio_put_masked(LED_MASK, 0); // released early: continue the ordinary boot
}
