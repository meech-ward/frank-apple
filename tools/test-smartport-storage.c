/* SPDX-License-Identifier: MIT */
#include "mii_bank.h"
#include "mii_dd.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { BLOCK_SIZE = 512, IMAGE_SIZE = 8 * BLOCK_SIZE };
static uint8_t images[2][IMAGE_SIZE], ram[65536];
static FIL *handles[2];
static unsigned seeks, writes, syncs;
static bool seek_fails, sync_fails;
static unsigned write_fails_at, short_write_at;

static unsigned drive_for(FIL *file) {
    for (unsigned drive = 0; drive < 2; ++drive)
        if (handles[drive] == file) return drive;
    assert(!"unexpected FatFs handle");
    return 0;
}

FRESULT f_open(FIL *file, const TCHAR *name, BYTE mode) {
    assert(mode == (FA_READ | FA_WRITE));
    unsigned drive = !strcmp(name, "/drive0.po") ? 0 : 1;
    assert(!strcmp(name, drive ? "/drive1.po" : "/drive0.po"));
    handles[drive] = file;
    file->fptr = 0;
    file->obj.objsize = IMAGE_SIZE;
    return FR_OK;
}

FRESULT f_close(FIL *file) {
    (void)file;
    return FR_OK;
}

FRESULT f_lseek(FIL *file, FSIZE_t offset) {
    (void)drive_for(file);
    ++seeks;
    if (seek_fails) return FR_DISK_ERR;
    assert(offset <= IMAGE_SIZE);
    file->fptr = offset;
    return FR_OK;
}

FRESULT f_read(FIL *file, void *data, UINT size, UINT *read) {
    unsigned drive = drive_for(file);
    assert(file->fptr + size <= IMAGE_SIZE);
    memcpy(data, images[drive] + file->fptr, size);
    file->fptr += size;
    *read = size;
    return FR_OK;
}

FRESULT f_write(FIL *file, const void *data, UINT size, UINT *written) {
    unsigned drive = drive_for(file);
    ++writes;
    *written = 0;
    if (writes == write_fails_at) return FR_DISK_ERR;
    assert(size == BLOCK_SIZE);
    assert(file->fptr + size <= IMAGE_SIZE);
    *written = writes == short_write_at ? size / 2 : size;
    memcpy(images[drive] + file->fptr, data, *written);
    file->fptr += *written;
    return FR_OK;
}

FRESULT f_sync(FIL *file) {
    (void)drive_for(file);
    ++syncs;
    return sync_fails ? FR_DISK_ERR : FR_OK;
}

static void reset_io(void) {
    seeks = writes = syncs = 0;
    seek_fails = sync_fails = false;
    write_fails_at = short_write_at = 0;
    memset(images, 0xcc, sizeof(images));
}

static void unchanged(const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; ++i) assert(data[i] == 0xcc);
}

int main(void) {
    mii_dd_system_t system;
    mii_dd_t drives[2] = {0};
    mii_bank_t bank = {.ua.raw = ram};
    mii_dd_system_init(NULL, &system);
    for (unsigned drive = 0; drive < 2; ++drive) {
        drives[drive].file = mii_dd_file_load(&system,
            drive ? "/drive1.po" : "/drive0.po", drive);
        assert(drives[drive].file && !drives[drive].file->read_only);
    }
    const uint16_t address = 0x3100;
    for (unsigned i = 0; i < 2 * BLOCK_SIZE; ++i)
        ram[address + i] = (uint8_t)(i * 17 + i / BLOCK_SIZE);
    uint8_t expected[2 * BLOCK_SIZE];
    memcpy(expected, ram + address, sizeof(expected));

    /* A real two-block transfer preserves surrounding blocks and the other
     * drive. Read it back through the same production storage adapter. */
    for (unsigned drive = 0; drive < 2; ++drive) {
        reset_io();
        assert(mii_dd_write(&drives[drive], &bank, address, 2, 2) == 0);
        assert(seeks == 1 && writes == 2 && syncs == 1);
        assert(!memcmp(images[drive] + 2 * BLOCK_SIZE, expected, sizeof(expected)));
        unchanged(images[drive], 2 * BLOCK_SIZE);
        unchanged(images[drive] + 4 * BLOCK_SIZE, IMAGE_SIZE - 4 * BLOCK_SIZE);
        unchanged(images[1 - drive], IMAGE_SIZE);
        memset(ram + address, 0, sizeof(expected));
        assert(mii_dd_read(&drives[drive], &bank, address, 2, 2) == 0);
        assert(!memcmp(ram + address, expected, sizeof(expected)));
    }

    /* Data writes succeeding is insufficient: a FatFs sync error must reach
     * SmartPort's caller. Retrying after the error can complete successfully. */
    reset_io();
    sync_fails = true;
    assert(mii_dd_write(&drives[0], &bank, address, 2, 2) < 0);
    assert(seeks == 1 && writes == 2 && syncs == 1);
    sync_fails = false;
    assert(mii_dd_write(&drives[0], &bank, address, 2, 2) == 0);
    assert(writes == 4 && syncs == 2);

    reset_io();
    seek_fails = true;
    assert(mii_dd_write(&drives[0], &bank, address, 2, 2) < 0);
    assert(seeks == 1 && !writes && !syncs);
    unchanged(images[0], IMAGE_SIZE);

    for (unsigned failure_at = 1; failure_at <= 2; ++failure_at) {
        reset_io();
        write_fails_at = failure_at;
        assert(mii_dd_write(&drives[0], &bank, address, 2, 2) < 0);
        assert(writes == failure_at && !syncs);
        reset_io();
        short_write_at = failure_at;
        assert(mii_dd_write(&drives[0], &bank, address, 2, 2) < 0);
        assert(writes == failure_at && !syncs);
    }

    reset_io();
    drives[0].file->read_only = true;
    assert(mii_dd_write(&drives[0], &bank, address, 2, 2) < 0);
    drives[0].file = NULL;
    assert(mii_dd_write(&drives[0], &bank, address, 2, 2) < 0);
    assert(!seeks && !writes && !syncs);
    puts("PASS: SmartPort block round-trip, drive isolation, sync/seek/write/short-write failures, write guards.");
    return 0;
}
