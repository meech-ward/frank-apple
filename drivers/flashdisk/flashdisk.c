// flashdisk.c: FatFs physical drive 0 on a region of the on-board QSPI flash.
//
// Geometry: FLASHDISK_OFFSET (byte offset from start of flash) and
// FLASHDISK_SIZE (bytes) from src/board_config.h. Logical sector = 512 bytes.
// Reads use the cached XIP mapping. Writes do read-modify-write of whole
// 4 KB flash sectors through a static RAM buffer: each overlapped flash
// sector is erased and reprogrammed only if its new contents differ from
// what is already stored (FatFs rewrites FAT/directory sectors often, and
// skipping identical rewrites saves erase cycles). Writes are write-through,
// so CTRL_SYNC is a no-op.
//
// Called from core 0 only. No dynamic allocation, no floating point.
#include "flashdisk.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pico.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/multicore.h"

#include "ff.h"
#include "diskio.h"
#include "../../src/board_config.h"

#define FLASHDISK_SECTOR_SIZE 512u
#define FLASHDISK_SECTOR_COUNT (FLASHDISK_SIZE / FLASHDISK_SECTOR_SIZE)
// Erase block in logical sectors, reported via GET_BLOCK_SIZE.
#define FLASHDISK_BLOCK_SECTORS (FLASH_SECTOR_SIZE / FLASHDISK_SECTOR_SIZE)
// Timeout for parking core 1 while an erase/program runs.
#define FLASHDISK_SAFE_TIMEOUT_MS 1000u

// Compile-time sanity: the region must be a whole number of flash sectors
// and must not overlap the firmware linked into the first 4 MB.
static_assert((FLASHDISK_OFFSET % FLASH_SECTOR_SIZE) == 0, "flashdisk offset not erase aligned");
static_assert((FLASHDISK_SIZE % FLASH_SECTOR_SIZE) == 0, "flashdisk size not erase multiple");
static_assert(FLASHDISK_SIZE % FLASHDISK_SECTOR_SIZE == 0, "flashdisk size not sector multiple");

static_assert(FLASHDISK_OFFSET >= 4u * 1024u * 1024u, "flashdisk overlaps firmware");
static_assert(FLASHDISK_OFFSET <= PICO_FLASH_SIZE_BYTES &&
              FLASHDISK_SIZE <= PICO_FLASH_SIZE_BYTES - FLASHDISK_OFFSET, "flashdisk exceeds flash");

static bool s_ready = false;
static bool s_logged = false;
static uint32_t s_erase_count = 0;
// Static 4 KB staging buffer for read-modify-write (never on the stack).
static uint8_t s_sector_buf[FLASH_SECTOR_SIZE];

uint32_t flashdisk_offset(void) { return FLASHDISK_OFFSET; }
uint32_t flashdisk_size(void) { return FLASHDISK_SIZE; }
bool flashdisk_ready(void) { return s_ready; }
uint32_t flashdisk_erase_count(void) { return s_erase_count; }

// Erase + program of one 4 KB flash sector, sourcing bytes from s_sector_buf.
// Runs with the calling context made safe by the caller (see below).
static void flashdisk_do_erase_program(void *param) {
    uint32_t offs = *(const uint32_t *)param;
    flash_range_erase(offs, FLASH_SECTOR_SIZE);
    flash_range_program(offs, s_sector_buf, FLASH_SECTOR_SIZE);
}

// Runs one erase + program of the flash sector at absolute flash offset offs.
//
// Two paths, decided by whether core 1 is parked yet:
// - Normal path (after boot): core 1 runs core1_main() and has called
//   multicore_lockout_victim_init(), so flash_safe_execute() parks it via the
//   multicore lockout while the erase/program runs, and disables IRQs here.
// - Early-boot path: disk_loader_init() runs before multicore_launch_core1(),
//   so the other core is not a lockout victim yet and flash_safe_execute()
//   would refuse with PICO_ERROR_NOT_PERMITTED (see default_enter_safe_zone
//   in pico-sdk/src/rp2_common/pico_flash/flash.c). The second core is still
//   held in reset at that stage, so nothing else can touch the flash/XIP;
//   disabling IRQs on this core is sufficient, and the erase/program runs
//   directly under save_and_disable_interrupts()/restore_interrupts().
static DRESULT flashdisk_erase_program(uint32_t offs) {
    int rc;
    if (multicore_lockout_victim_is_initialized((uint)(1 - (int)get_core_num()))) {
        rc = flash_safe_execute(flashdisk_do_erase_program, &offs,
                                FLASHDISK_SAFE_TIMEOUT_MS);
    } else {
        uint32_t irq = save_and_disable_interrupts();
        flashdisk_do_erase_program(&offs);
        restore_interrupts(irq);
        rc = PICO_OK;
    }
    if (rc != PICO_OK) {
        printf("flashdisk: erase/program @0x%08lx failed rc=%d\n",
               (unsigned long)offs, rc);
        return RES_ERROR;
    }
    if (memcmp((const void *)(XIP_BASE + offs), s_sector_buf, FLASH_SECTOR_SIZE) != 0) {
        printf("flashdisk: verify failed @0x%08lx\n", (unsigned long)offs);
        return RES_ERROR;
    }
    s_erase_count++;
    return RES_OK;
}

static void flashdisk_log_once(void) {
    if (s_logged) {
        return;
    }
    s_logged = true;
    printf("flashdisk: offset 0x%08lx size %lu bytes, %lu sectors of %u bytes\n",
           (unsigned long)FLASHDISK_OFFSET, (unsigned long)FLASHDISK_SIZE,
           (unsigned long)FLASHDISK_SECTOR_COUNT, FLASHDISK_SECTOR_SIZE);
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }
    s_ready = true;
    flashdisk_log_once();
    return 0;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || count == 0) {
        return RES_PARERR;
    }
    if (sector >= FLASHDISK_SECTOR_COUNT || count > FLASHDISK_SECTOR_COUNT - sector) {
        return RES_PARERR;
    }
    const uint8_t *base =
        (const uint8_t *)(XIP_BASE + FLASHDISK_OFFSET + (uint32_t)sector * FLASHDISK_SECTOR_SIZE);
    memcpy(buff, base, (size_t)count * FLASHDISK_SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || count == 0) {
        return RES_PARERR;
    }
    if (sector >= FLASHDISK_SECTOR_COUNT || count > FLASHDISK_SECTOR_COUNT - sector) {
        return RES_PARERR;
    }

    uint32_t start = (uint32_t)sector * FLASHDISK_SECTOR_SIZE;
    uint32_t end = start + (uint32_t)count * FLASHDISK_SECTOR_SIZE;
    uint32_t first = start / FLASH_SECTOR_SIZE;
    uint32_t last = (end - 1u) / FLASH_SECTOR_SIZE;

    for (uint32_t fs = first; fs <= last; fs++) {
        uint32_t fs_base = fs * FLASH_SECTOR_SIZE;
        uint32_t abs_offs = FLASHDISK_OFFSET + fs_base;
        const uint8_t *current = (const uint8_t *)(XIP_BASE + abs_offs);

        memcpy(s_sector_buf, current, FLASH_SECTOR_SIZE);

        // Overlay the 512-byte logical sectors of this request that fall
        // inside the current 4 KB flash sector.
        for (UINT i = 0; i < count; i++) {
            uint32_t lo = ((uint32_t)sector + i) * FLASHDISK_SECTOR_SIZE - fs_base;
            if (lo < FLASH_SECTOR_SIZE) {
                memcpy(&s_sector_buf[lo], &buff[(size_t)i * FLASHDISK_SECTOR_SIZE],
                       FLASHDISK_SECTOR_SIZE);
            }
        }

        // Skip identical rewrites: no erase cycle spent.
        if (memcmp(s_sector_buf, current, FLASH_SECTOR_SIZE) == 0) {
            continue;
        }

        DRESULT rc = flashdisk_erase_program(abs_offs);
        if (rc != RES_OK) {
            return rc;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0) {
        return RES_PARERR;
    }
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        // LBA_t is 64 bits when FF_LBA64 is set: fill the whole object, or
        // the caller keeps garbage in the high half (it passes &LBA_t).
        *(LBA_t *)buff = (LBA_t)FLASHDISK_SECTOR_COUNT;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = (WORD)FLASHDISK_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = (DWORD)FLASHDISK_BLOCK_SECTORS;
        return RES_OK;
    case CTRL_TRIM:
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

// Fixed timestamp: 2026-09-20 00:00:00 (no RTC wired yet).
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)9 << 21) | ((DWORD)20 << 16);
}
