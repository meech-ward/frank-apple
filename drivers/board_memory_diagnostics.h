// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sam Meech-Ward
#ifndef FRANK_BOARD_MEMORY_DIAGNOSTICS_H
#define FRANK_BOARD_MEMORY_DIAGNOSTICS_H

#include <stdbool.h>

#ifndef BOARD_MEMORY_DIAGNOSTICS
#define BOARD_MEMORY_DIAGNOSTICS 0
#endif

#if BOARD_MEMORY_DIAGNOSTICS && PSRAM_MAX_FREQ_MHZ
// DESTRUCTIVE: call only at boot after memory initialization and before any
// disk buffers, graphics DMA, or core 1 use PSRAM. Tests the complete chip.
bool external_memory_diagnostic_boot(void);

// Diagnostic builds reserve the LAST 4096 bytes of PSRAM for this sentinel.
// The caller must ensure disk buffers or other users never occupy this range.
// Prepare immediately before a real FatFs write+sync. It deliberately leaves
// cached writes pending and reports how many are absent from physical PSRAM.
bool external_memory_diagnostic_prepare_sentinel(void);

// Call after the FatFs operation. Reads physical PSRAM without first cleaning
// the cache, so a missing pre-flash cache clean cannot be hidden by this test.
bool external_memory_diagnostic_verify_sentinel(void);
#endif

#endif
