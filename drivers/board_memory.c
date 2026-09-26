// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sam Meech-Ward
#include "board_memory.h"

#if PSRAM_MAX_FREQ_MHZ
#if !PICO_RP2350
#error External QMI memory requires RP2350
#endif

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "vendor/pico_psram/psram_qmi.h"

static size_t memory_size;

// Every instruction executed with QMI direct mode enabled must reside in SRAM.
// One byte in flight keeps the receive FIFO bounded, including command replies.
static uint8_t __no_inline_not_in_flash_func(memory_exchange)(uint32_t tx) {
    qmi_hw->direct_tx = tx;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_RXEMPTY_BITS) tight_loop_contents();
    return (uint8_t)qmi_hw->direct_rx;
}

static void __no_inline_not_in_flash_func(memory_deselect)(uint32_t direct_config) {
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) tight_loop_contents();
    qmi_hw->direct_csr = direct_config;
    // More than the required 18 ns at supported system clocks. Volatile reads
    // keep this delay inside SRAM without invoking any flash-resident helper.
    for (unsigned i = 0; i < 8; ++i) (void)qmi_hw->direct_csr;
}

static void __no_inline_not_in_flash_func(memory_read_id)(uint32_t divider, uint8_t id[8]) {
    uint32_t direct_config = (divider << QMI_DIRECT_CSR_CLKDIV_LSB) | QMI_DIRECT_CSR_EN_BITS;
    qmi_hw->direct_csr = direct_config;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) tight_loop_contents();

    // APS6404 Quad Mode Exit: recover the chip's SPI state after a warm reset.
    qmi_hw->direct_csr = direct_config | QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    (void)memory_exchange(QMI_DIRECT_TX_OE_BITS |
            (QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB) | 0xf5u);
    memory_deselect(direct_config);

    // Read ID: command, three address bytes, then manufacturer/KGD/EID bytes.
    qmi_hw->direct_csr = direct_config | QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    for (unsigned i = 0; i < 8; ++i) id[i] = memory_exchange(i == 0 ? 0x9fu : 0xffu);
    memory_deselect(direct_config);
    qmi_hw->direct_csr = 0;
}

static void __no_inline_not_in_flash_func(memory_enter_quad)(uint32_t divider) {
    uint32_t direct_config = (divider << QMI_DIRECT_CSR_CLKDIV_LSB) | QMI_DIRECT_CSR_EN_BITS;
    qmi_hw->direct_csr = direct_config;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) tight_loop_contents();
    qmi_hw->direct_csr = direct_config | QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    (void)memory_exchange(0x35u);
    memory_deselect(direct_config);
    qmi_hw->direct_csr = 0;
}

bool external_memory_init(unsigned cs_pin) {
    if (memory_size) return true;
    // Only hardware-supported CS1 pins, with no live direct-mode transaction.
    if (cs_pin != 0 && cs_pin != 8 && cs_pin != 19
#if !PICO_RP2350A
            && cs_pin != 47
#endif
    ) return false;
    if (qmi_hw->direct_csr & QMI_DIRECT_CSR_EN_BITS) return false;
    // SDK 2.2.0 restores custom QMI CS1 setup across flash writes only when the
    // ROM does not own CS1. Do not opt into newer SDK flash-devinfo behavior.
    if (flash_devinfo_get_cs_size(1) != FLASH_DEVINFO_SIZE_NONE) return false;

    uint32_t clock_hz = clock_get_hz(clk_sys);
    pico_psram_params params;
    if (!pico_psram_calculate_params(clock_hz, PSRAM_MAX_FREQ_MHZ * 1000000u, &params)) return false;
    // The serial Read ID command is limited to 33 MHz; use at most 25 MHz.
    uint32_t divider = (clock_hz + 24999999u) / 25000000u;
    gpio_function_t old_function = gpio_get_function(cs_pin);
    uint32_t interrupts = save_and_disable_interrupts();
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);
    uint8_t id[8];
    memory_read_id(divider, id);
    size_t detected_size = pico_psram_capacity(id[5], id[6]);
    if (detected_size) {
        memory_enter_quad(divider);
        pico_psram_configure_qmi(&params);
        memory_size = detected_size;
    } else {
        gpio_set_function(cs_pin, old_function);
    }
    restore_interrupts(interrupts);
    return memory_size != 0;
}

size_t external_memory_size(void) { return memory_size; }
#endif
