/* SPDX-License-Identifier: MIT
 * Native helper for configure-wifi.py. The actual vendored FatFs edits a RAM
 * copy; the input image is never writable. Every unrelated file is compared
 * with the original through FatFs before the resulting image can be saved. */
#include "wifi_config.h"
#include "ff.h"
#include "diskio.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTORS 24576u
#define IMAGE_SIZE (SECTORS * 512u)
#define PATH_SIZE 1024
static BYTE media[2][IMAGE_SIZE];
static unsigned checked_files;

DSTATUS disk_initialize(BYTE drive) { return drive < 2 ? 0 : STA_NOINIT; }
DSTATUS disk_status(BYTE drive) { return drive < 2 ? 0 : STA_NOINIT; }
DRESULT disk_read(BYTE drive, BYTE *out, LBA_t sector, UINT count) {
    if (drive >= 2 || sector >= SECTORS || count > SECTORS-sector) return RES_PARERR;
    memcpy(out, media[drive] + sector*512u, count*512u);
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *data, LBA_t sector, UINT count) {
    if (drive != 1 || sector >= SECTORS || count > SECTORS-sector) return RES_WRPRT;
    memcpy(media[drive] + sector*512u, data, count*512u);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *out) {
    if (drive >= 2) return RES_PARERR;
    switch (command) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)out = SECTORS; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)out = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)out = 8; return RES_OK;
    default: return RES_PARERR;
    }
}
DWORD get_fattime(void) { return (2026u-1980u)<<25 | 1u<<21 | 1u<<16; }

static int failure(const char *message) {
    fprintf(stderr, "wifi-image-edit: %s; input image was not changed\n", message);
    return 1;
}

static bool read_config(const char *path, char text[WIFI_CONFIG_MAX+1], size_t *length) {
    FILE *input = fopen(path, "rb");
    if (!input) return false;
    *length = fread(text, 1, WIFI_CONFIG_MAX+1, input);
    bool ok = !ferror(input);
    if (fclose(input)) ok = false;
    wifi_config config;
    return ok && wifi_config_parse(text, *length, &config);
}

static bool is_wifi(const char *name) {
    const char *expected = "WIFI.INI";
    while (*name && *expected) {
        if (toupper((unsigned char)*name++) != *expected++) return false;
    }
    return !*name && !*expected;
}

static bool compare_files(const char *left, const char *right) {
    FIL a, b;
    if (f_open(&a, left, FA_READ) != FR_OK) return false;
    if (f_open(&b, right, FA_READ) != FR_OK) { f_close(&a); return false; }
    bool ok = f_size(&a) == f_size(&b);
    BYTE x[4096], y[4096]; UINT nx, ny;
    while (ok) {
        if (f_read(&a, x, sizeof(x), &nx) != FR_OK ||
                f_read(&b, y, sizeof(y), &ny) != FR_OK || nx != ny || memcmp(x, y, nx)) {
            ok = false; break;
        }
        if (!nx) break;
    }
    if (f_close(&a) != FR_OK) ok = false;
    if (f_close(&b) != FR_OK) ok = false;
    return ok;
}

static bool compare_tree(unsigned source, unsigned target, const char *path, unsigned depth) {
    if (depth > 16) return false;
    char from[PATH_SIZE], to[PATH_SIZE], child[PATH_SIZE];
    if (snprintf(from, sizeof(from), "%u:%s", source, path) >= (int)sizeof(from)) return false;
    DIR directory;
    if (f_opendir(&directory, from) != FR_OK) return false;
    bool ok = true;
    for (;;) {
        FILINFO entry, other;
        if (f_readdir(&directory, &entry) != FR_OK) { ok = false; break; }
        if (!entry.fname[0]) break;
        if (!strcmp(entry.fname, ".") || !strcmp(entry.fname, "..")) continue;
        if (!strcmp(path, "/") && is_wifi(entry.fname)) continue;
        if (snprintf(child, sizeof(child), "%s%s%s", path,
                strcmp(path, "/") ? "/" : "", entry.fname) >= (int)sizeof(child) ||
                snprintf(from, sizeof(from), "%u:%s", source, child) >= (int)sizeof(from) ||
                snprintf(to, sizeof(to), "%u:%s", target, child) >= (int)sizeof(to) ||
                f_stat(to, &other) != FR_OK || entry.fsize != other.fsize ||
                entry.fattrib != other.fattrib || entry.fdate != other.fdate ||
                entry.ftime != other.ftime) { ok = false; break; }
        if (entry.fattrib & AM_DIR) ok = compare_tree(source, target, child, depth+1);
        else {
            ok = compare_files(from, to);
            if (source == 0) ++checked_files;
        }
        if (!ok) break;
    }
    if (f_closedir(&directory) != FR_OK) ok = false;
    return ok;
}

int main(int argc, char **argv) {
    bool validate_only = argc == 3 && !strcmp(argv[1], "--validate");
    if (!validate_only && argc != 4) return failure("expected input.img wifi.ini output.img");
    char config_text[WIFI_CONFIG_MAX+1]; size_t config_length;
    if (!read_config(argv[2], config_text, &config_length)) return failure("invalid or unreadable wifi.ini");
    if (validate_only) return 0;
    FILE *input = fopen(argv[1], "rb");
    if (!input) return failure("cannot read input image");
    bool read_ok = fread(media[0], 1, IMAGE_SIZE, input) == IMAGE_SIZE && fgetc(input) == EOF && !ferror(input);
    if (fclose(input)) read_ok = false;
    if (!read_ok) return failure("expected an exact 12 MiB data image");
    memcpy(media[1], media[0], IMAGE_SIZE);
    FATFS before, after;
    if (f_mount(&before, "0:", 1) != FR_OK || f_mount(&after, "1:", 1) != FR_OK ||
            before.fs_type != FS_FAT16 || before.volbase != 0) return failure("expected the badge FAT16 data volume");
    FILINFO info;
    if (f_stat("0:/apple", &info) != FR_OK || !(info.fattrib & AM_DIR))
        return failure("not an Apple II data volume (missing /apple directory)");
    FIL file; UINT count;
    if (f_open(&file, "1:/wifi.ini", FA_WRITE|FA_CREATE_ALWAYS) != FR_OK)
        return failure("cannot open wifi.ini in the image copy");
    bool write_ok = f_write(&file, config_text, (UINT)config_length, &count) == FR_OK && count == config_length;
    if (f_close(&file) != FR_OK) write_ok = false;
    if (!write_ok) return failure("not enough writable space for wifi.ini");
    if (f_mount(NULL, "1:", 0) != FR_OK || f_mount(&after, "1:", 1) != FR_OK)
        return failure("cannot remount the updated volume");
    char check[WIFI_CONFIG_MAX+1];
    if (f_open(&file, "1:/wifi.ini", FA_READ) != FR_OK) return failure("cannot reopen updated wifi.ini");
    bool config_ok = f_size(&file) == config_length &&
        f_read(&file, check, sizeof(check), &count) == FR_OK && count == config_length &&
        !memcmp(check, config_text, config_length);
    if (f_close(&file) != FR_OK) config_ok = false;
    if (!config_ok) return failure("wifi.ini readback failed");
    /* Compare in both directions: no other file may change, disappear, or appear. */
    if (!compare_tree(0, 1, "/", 0) || !compare_tree(1, 0, "/", 0))
        return failure("unrelated file verification failed");
    if (f_mount(NULL, "0:", 0) != FR_OK || f_mount(NULL, "1:", 0) != FR_OK)
        return failure("cannot unmount the verified volumes");
    FILE *output = fopen(argv[3], "wbx");
    if (!output) return failure("output already exists or cannot be created");
    bool output_ok = fwrite(media[1], 1, IMAGE_SIZE, output) == IMAGE_SIZE;
    if (fclose(output)) output_ok = false;
    if (!output_ok) { remove(argv[3]); return failure("cannot save updated image"); }
    printf("Updated /wifi.ini; verified %u other files unchanged.\n", checked_files);
    return 0;
}
