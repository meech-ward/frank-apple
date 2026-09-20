#pragma once
// FatFs physical drive 0 on a region of the QSPI flash (see FLASHDISK_OFFSET /
// FLASHDISK_SIZE in src/board_config.h). Logical sector = 512 bytes; the
// driver does read-modify-write of 4 KB flash sectors and runs every
// erase/program through flash_safe_execute(), so core 1 must have called
// multicore_lockout_victim_init() before the first write.
#include <stdint.h>
#include <stdbool.h>

// Region geometry, for logging and for the image-flashing tools.
uint32_t flashdisk_offset(void);
uint32_t flashdisk_size(void);
// True after the FAT has been mounted through disk_initialize() at least once.
bool flashdisk_ready(void);
// Erase and program counters since boot (receipts for the write path).
uint32_t flashdisk_erase_count(void);
