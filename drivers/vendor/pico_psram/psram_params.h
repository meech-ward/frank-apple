/*
 * Copyright (c) 2026 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 * Adapted from pico-sdk hardware_psram; see README.md for the pinned source.
 */
#ifndef FRANK_PICO_PSRAM_PARAMS_H
#define FRANK_PICO_PSRAM_PARAMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t divisor;
    uint32_t rxdelay;
    uint32_t max_select;
    uint32_t min_deselect;
} pico_psram_params;

static inline size_t pico_psram_capacity(uint8_t kgd, uint8_t eid) {
    if (kgd != 0x5d) return 0;
    uint8_t size_id = eid >> 5;
    if (size_id == 4) return 16u * 1024u * 1024u;
    if (eid == 0x26 || size_id == 2 || size_id == 3) return 8u * 1024u * 1024u;
    if (size_id == 1) return 4u * 1024u * 1024u;
    if (size_id == 0) return 2u * 1024u * 1024u;
    // Unlike upstream's fallback to 1 MiB, unknown density codes fail closed.
    return 0;
}

static inline bool pico_psram_calculate_params(uint32_t clock_hz,
        uint32_t max_psram_freq, pico_psram_params *out) {
    if (!clock_hz || !max_psram_freq || !out) return false;
    uint64_t divisor = ((uint64_t)clock_hz + max_psram_freq - 1) / max_psram_freq;
    if (divisor == 1 && clock_hz > 100000000) divisor = 2;
    uint64_t rxdelay = divisor;
    if (clock_hz / divisor > 100000000) ++rxdelay;

    // APS6404 timing: maximum select 8 us, minimum deselect 18 ns.
    // MAX_SELECT is measured in units of 64 system clocks.
    uint64_t clock_period_fs = 1000000000000000ull / clock_hz;
    uint64_t max_select = 8000000000ull / (64ull * clock_period_fs);
    uint64_t deselect_cycles = (18000000ull + clock_period_fs - 1) / clock_period_fs;
    uint64_t half_divisor = (divisor + 1) / 2;
    uint64_t min_deselect = deselect_cycles > half_divisor ? deselect_cycles - half_divisor : 0;

    // Reject unrepresentable values instead of spilling into adjacent fields.
    // Zero MAX_SELECT disables refresh protection, so it is also invalid.
    if (divisor > 255 || rxdelay > 7 || !max_select || max_select > 63 || min_deselect > 31)
        return false;
    *out = (pico_psram_params) {
        (uint32_t)divisor, (uint32_t)rxdelay,
        (uint32_t)max_select, (uint32_t)min_deselect
    };
    return true;
}

#endif
