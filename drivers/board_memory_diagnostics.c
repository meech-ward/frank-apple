// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sam Meech-Ward
#include "board_memory.h"
#include "board_memory_diagnostics.h"

#if BOARD_MEMORY_DIAGNOSTICS && PSRAM_MAX_FREQ_MHZ
#include <inttypes.h>
#include <stdio.h>
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "hardware/xip_cache.h"
#include "pico/time.h"
#include "../src/disk_loader.h"

#define SENTINEL_BYTES 4096u
#define PSRAM_XIP_OFFSET ((uintptr_t)EXTERNAL_MEMORY_DATA - XIP_BASE)

static bool diagnostic_size_valid(size_t bytes) {
    // Current disk-loader users occupy exactly two BDSK buffers. The HDD base
    // macro in mii_dd_stub.c is unused; HDD transfers use a 512-byte SRAM buffer.
    // Fail before touching the sentinel if those live buffers would overlap it.
    return bytes >= 2u * BDSK_BYTES + SENTINEL_BYTES &&
            bytes <= 16u * 1024u * 1024u && bytes % XIP_CACHE_LINE_SIZE == 0;
}

static volatile uint32_t *physical_memory(void) {
    return (volatile uint32_t *)(XIP_NOCACHE_NOALLOC_BASE + PSRAM_XIP_OFFSET);
}

static uint32_t diagnostic_pattern(uint32_t word, unsigned pass) {
    switch (pass) {
    case 0: return 0;
    case 1: return UINT32_MAX;
    case 2: return 0x55555555u;
    case 3: return 0xaaaaaaaau;
    default: {
        // Odd multiplication is one-to-one modulo 2^32: aliased addresses
        // cannot have the same expected value, including across chip halves.
        uint32_t value = word * 0x9e3779b1u ^ 0x62d4b83fu;
        return pass == 4 ? value : ~value;
    }
    }
}

bool external_memory_diagnostic_boot(void) {
    size_t bytes = external_memory_size();
    if (!diagnostic_size_valid(bytes)) return false;
    volatile uint32_t *memory = physical_memory();
    uint32_t words = (uint32_t)(bytes / sizeof(uint32_t));
    uint32_t total_errors = 0;
    uint64_t started = time_us_64();
    // Commit the existing startup probe before changing memory uncached.
    xip_cache_clean_all();
    xip_cache_invalidate_all();
    printf("PSRAM DIAG begin bytes=%lu passes=6 physical=%p\n",
            (unsigned long)bytes, (void *)memory);
    for (unsigned pass = 0; pass < 6; ++pass) {
        uint32_t errors = 0, first_word = 0, first_expected = 0, first_actual = 0;
        for (uint32_t word = 0; word < words; ++word)
            memory[word] = diagnostic_pattern(word, pass);
        __dsb();
        for (uint32_t word = 0; word < words; ++word) {
            uint32_t expected = diagnostic_pattern(word, pass);
            uint32_t actual = memory[word];
            if (actual != expected) {
                if (!errors) {
                    first_word = word;
                    first_expected = expected;
                    first_actual = actual;
                }
                ++errors;
            }
        }
        total_errors += errors;
        printf("PSRAM DIAG pass=%u words=%" PRIu32 " errors=%" PRIu32 "\n", pass, words, errors);
        if (errors)
            printf("PSRAM DIAG first offset=0x%08" PRIx32 " expected=%08" PRIx32 " actual=%08" PRIx32 "\n",
                    first_word * 4u, first_expected, first_actual);
    }
    __dsb();
    xip_cache_invalidate_all();
    printf("PSRAM DIAG %s bytes=%lu checks=%" PRIu32 " errors=%" PRIu32 " ms=%lu\n",
            total_errors ? "FAIL" : "PASS", (unsigned long)bytes,
            words * 6u, total_errors, (unsigned long)((time_us_64() - started) / 1000));
    return total_errors == 0;
}

static uint32_t sentinel_generation;
static uint32_t sentinel_value(uint32_t word) {
    return diagnostic_pattern(word, 4) ^ sentinel_generation;
}

bool external_memory_diagnostic_prepare_sentinel(void) {
    size_t bytes = external_memory_size();
    if (!diagnostic_size_valid(bytes)) return false;
    size_t offset = bytes - SENTINEL_BYTES;
    volatile uint32_t *physical = physical_memory() + offset / sizeof(uint32_t);
    volatile uint32_t *cached = (volatile uint32_t *)(EXTERNAL_MEMORY_DATA + offset);
    ++sentinel_generation;
    // Only this reserved range is maintained: live disk buffers are untouched.
    xip_cache_clean_range(PSRAM_XIP_OFFSET + offset, SENTINEL_BYTES);
    xip_cache_invalidate_range(PSRAM_XIP_OFFSET + offset, SENTINEL_BYTES);
    for (uint32_t word = 0; word < SENTINEL_BYTES / sizeof(uint32_t); ++word)
        physical[word] = ~sentinel_value(word);
    __dsb();
    for (uint32_t word = 0; word < SENTINEL_BYTES / sizeof(uint32_t); ++word) {
        (void)cached[word]; // Allocate the cache line before making it dirty.
        cached[word] = sentinel_value(word);
    }
    __dsb();
    uint32_t pending = 0, errors = 0;
    for (uint32_t word = 0; word < SENTINEL_BYTES / sizeof(uint32_t); ++word) {
        if (cached[word] != sentinel_value(word)) ++errors;
        if (physical[word] != sentinel_value(word)) ++pending;
    }
    printf("PSRAM SENTINEL prepared offset=0x%08lx bytes=%u cached_errors=%" PRIu32 " pending_words=%" PRIu32 "\n",
            (unsigned long)offset, SENTINEL_BYTES, errors, pending);
    // With no pending writes the cache-clean part of the test is inconclusive.
    return errors == 0 && pending != 0;
}

bool external_memory_diagnostic_verify_sentinel(void) {
    size_t bytes = external_memory_size();
    if (!diagnostic_size_valid(bytes) || !sentinel_generation) return false;
    volatile uint32_t *physical = physical_memory() + (bytes - SENTINEL_BYTES) / sizeof(uint32_t);
    uint32_t errors = 0;
    for (uint32_t word = 0; word < SENTINEL_BYTES / sizeof(uint32_t); ++word)
        if (physical[word] != sentinel_value(word)) ++errors;
    printf("PSRAM SENTINEL %s words=%u errors=%" PRIu32 "\n",
            errors ? "FAIL" : "PASS", SENTINEL_BYTES / (unsigned)sizeof(uint32_t), errors);
    return errors == 0;
}
#endif
