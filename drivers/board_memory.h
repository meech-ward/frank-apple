// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sam Meech-Ward
#ifndef FRANK_BOARD_MEMORY_H
#define FRANK_BOARD_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXTERNAL_MEMORY_DATA ((uint8_t *)(uintptr_t)0x11000000u)

#if PSRAM_MAX_FREQ_MHZ
// Startup only: call after setting clk_sys and before launching core 1 or DMA.
// Detects capacity without modifying memory. Returns false on unsupported hardware.
bool external_memory_init(unsigned cs_pin);
size_t external_memory_size(void);
#else
static inline bool external_memory_init(unsigned cs_pin) {
    (void)cs_pin;
    return false;
}
static inline size_t external_memory_size(void) { return 0; }
#endif

#endif
