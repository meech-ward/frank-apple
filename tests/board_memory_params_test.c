// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sam Meech-Ward
#include <assert.h>
#include <stdio.h>
#include "drivers/vendor/pico_psram/psram_params.h"

static void test_known_boards(void) {
    pico_psram_params p;
    assert(pico_psram_calculate_params(252000000, 84000000, &p));
    assert(p.divisor == 3 && p.rxdelay == 3 && p.max_select == 31 && p.min_deselect == 3);
    assert(pico_psram_calculate_params(252000000, 133000000, &p));
    assert(p.divisor == 2 && p.rxdelay == 3 && p.max_select == 31 && p.min_deselect == 4);
    assert(pico_psram_calculate_params(100000000, 133000000, &p));
    assert(p.divisor == 1 && p.rxdelay == 1);
    assert(pico_psram_calculate_params(101000000, 133000000, &p));
    assert(p.divisor == 2 && p.rxdelay == 2);
    // At a slow clock the normal half-cycle deselect already exceeds 18 ns.
    assert(pico_psram_calculate_params(12000000, 2000000, &p));
    assert(p.min_deselect == 0);
}

static void test_timing_bounds(void) {
    pico_psram_params p = {123, 123, 123, 123};
    assert(!pico_psram_calculate_params(0, 84000000, &p));
    assert(!pico_psram_calculate_params(252000000, 0, &p));
    assert(!pico_psram_calculate_params(252000000, 84000000, NULL));
    assert(!pico_psram_calculate_params(1000000, 84000000, &p)); // refresh field would be zero
    assert(!pico_psram_calculate_params(600000000, 133000000, &p)); // refresh field too large
    assert(!pico_psram_calculate_params(252000000, 1000000, &p)); // RXDELAY too large
    assert(!pico_psram_calculate_params(UINT32_MAX, UINT32_MAX, &p));
    assert(p.divisor == 123 && p.rxdelay == 123 && p.max_select == 123 && p.min_deselect == 123);

    unsigned checked = 0;
    const uint32_t target_mhz[] = {42, 84, 100, 133, 166};
    for (uint32_t mhz = 12; mhz <= 500; ++mhz) {
        for (size_t i = 0; i < sizeof(target_mhz) / sizeof(target_mhz[0]); ++i) {
            uint32_t hz = mhz * 1000000u;
            if (!pico_psram_calculate_params(hz, target_mhz[i] * 1000000u, &p)) continue;
            assert((uint64_t)target_mhz[i] * 1000000u * p.divisor >= hz);
            assert(p.divisor && p.divisor <= 255 && p.rxdelay <= 7);
            assert(p.max_select && p.max_select <= 63 && p.min_deselect <= 31);
            assert((uint64_t)p.max_select * 64u * 1000000000u <= (uint64_t)8000u * hz);
            assert((uint64_t)(p.min_deselect + (p.divisor + 1) / 2) * 1000000000u >= (uint64_t)18u * hz);
            ++checked;
        }
    }
    assert(checked > 2000);
}

static void test_capacity(void) {
    assert(pico_psram_capacity(0x5d, 0x00) == 2u * 1024u * 1024u);
    assert(pico_psram_capacity(0x5d, 0x20) == 4u * 1024u * 1024u);
    assert(pico_psram_capacity(0x5d, 0x26) == 8u * 1024u * 1024u);
    assert(pico_psram_capacity(0x5d, 0x52) == 8u * 1024u * 1024u);
    assert(pico_psram_capacity(0x5d, 0x60) == 8u * 1024u * 1024u);
    assert(pico_psram_capacity(0x5d, 0x80) == 16u * 1024u * 1024u);
    assert(pico_psram_capacity(0xff, 0xff) == 0); // disconnected bus
    assert(pico_psram_capacity(0x00, 0x00) == 0);
    for (unsigned eid = 0xa0; eid <= 0xff; ++eid) assert(pico_psram_capacity(0x5d, eid) == 0);
}

int main(void) {
    test_known_boards();
    test_timing_bounds();
    test_capacity();
    puts("board memory timing and capacity tests passed");
    return 0;
}
