// SPDX-License-Identifier: MIT
// Original downstream parser. Reads disk metadata; never modifies Apple memory/media.
#include "dos_catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool valid_name(const char *name) {
    size_t n = 0;
    if (!name || name[0] < 'A' || name[0] > 'Z') return false;
    for (; n < 31 && name[n]; ++n)
        if ((unsigned char)name[n] < 32 || (unsigned char)name[n] > 126 ||
            name[n] == ',' || name[n] == ':') return false;
    return n > 0 && n <= 30 && name[n - 1] != ' ';
}

bool dos_program_command(char *out, size_t capacity, const char *name, bool run) {
    if (!out || !capacity) return false;
    out[0] = 0;
    if (!valid_name(name)) return false;
    int n = snprintf(out, capacity, "%s %s,S6,D1\r", run ? "RUN" : "LOAD", name);
    if (n < 0 || (size_t)n >= capacity) { out[0] = 0; return false; }
    return true;
}

static int compare(const void *a, const void *b) {
    return strcmp(((const dos_program_t *)a)->name, ((const dos_program_t *)b)->name);
}

int dos_catalog_scan(dos_sector_reader read, void *context,
                     dos_program_t *programs, size_t capacity) {
    uint8_t block[256], visited[35 * 16 / 8] = {0};
    if (!read || !programs || !read(context, 17, 0, block)) return -1;
    // Standard 35-track DOS 3.3 VTOC: version, T/S-list capacity, geometry.
    if (block[3] != 3 || block[0x27] != 122 || block[0x34] != 35 ||
        block[0x35] != 16 || block[0x36] != 0 || block[0x37] != 1) return -1;
    unsigned track = block[1], sector = block[2];
    size_t count = 0;
    while (track) {
        if (track >= 35 || sector >= 16) return -1;
        unsigned bit = track * 16 + sector;
        if (visited[bit / 8] & (1u << (bit % 8))) return -1;
        visited[bit / 8] |= 1u << (bit % 8);
        if (!read(context, track, sector, block)) return -1;
        track = block[1]; sector = block[2];
        for (unsigned offset = 11; offset + 35 <= 256; offset += 35) {
            const uint8_t *entry = block + offset;
            if (!entry[0] || entry[0] == 0xff || (entry[2] & 0x7f) != 2) continue;
            if (entry[0] >= 35 || entry[1] >= 16) return -1;
            dos_program_t program;
            for (unsigned i = 0; i < 30; ++i) program.name[i] = entry[3+i] & 0x7f;
            program.name[30] = 0;
            for (int i = 29; i >= 0 && program.name[i] == ' '; --i) program.name[i] = 0;
            // Embedded NUL/control bytes and DOS delimiters are not launchable.
            bool invalid = false;
            for (unsigned i = 0; i < 30; ++i)
                if ((entry[3+i] & 0x7f) < 32) invalid = true;
            if (invalid || !valid_name(program.name)) continue;
            if (count >= capacity) return -1;
            programs[count++] = program;
        }
    }
    if (sector) return -1;
    qsort(programs, count, sizeof(*programs), compare);
    return (int)count;
}

static uint32_t bits_at(const uint8_t *data, uint32_t count, uint32_t pos, unsigned n) {
    uint32_t value = 0;
    pos %= count;
    while (n--) {
        value = (value << 1) | ((data[pos / 8] >> (7 - pos % 8)) & 1);
        if (++pos == count) pos = 0;
    }
    return value;
}

static bool read_nibble(const uint8_t *bits, uint32_t count, uint32_t *pos,
                        uint32_t limit, uint8_t *out) {
    // Disk II latches a byte when its top bit becomes set. Freshly written
    // sectors need not remain byte aligned like a converted .dsk image.
    while (*pos < limit && !bits_at(bits, count, *pos, 1)) ++*pos;
    if (*pos + 8 > limit) return false;
    *out = bits_at(bits, count, *pos, 8);
    *pos += 8;
    return true;
}

bool dos_catalog_sector(const uint8_t *bits, uint32_t count, uint8_t track,
                        uint8_t sector, uint8_t out[256]) {
    static const uint8_t physical_to_dos[16] =
        {0,7,14,6,13,5,12,4,11,3,10,2,9,1,8,15};
    static const uint8_t gcr[64] = {
        0x96,0x97,0x9a,0x9b,0x9d,0x9e,0x9f,0xa6,0xa7,0xab,0xac,0xad,0xae,0xaf,0xb2,0xb3,
        0xb4,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xcb,0xcd,0xce,0xcf,0xd3,
        0xd6,0xd7,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe5,0xe6,0xe7,0xe9,0xea,0xeb,0xec,
        0xed,0xee,0xef,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
    };
    if (!bits || !out || count < 3000 || count > 6656 * 8 || track >= 35 || sector >= 16)
        return false;
    uint8_t decode[256]; memset(decode, 0xff, sizeof(decode));
    for (unsigned i = 0; i < 64; ++i) decode[gcr[i]] = i;
    // Every search is bounded, including corrupt tracks with no sync/data.
    uint32_t window = bits_at(bits, count, count - 23, 23);
    for (uint32_t end = 0; end < count; ++end) {
        window = ((window << 1) | ((bits[end / 8] >> (7-end%8)) & 1)) & 0xffffff;
        if (window != 0xd5aa96) continue;
        uint32_t pos = end + 1;
        uint8_t header[4]; bool valid = true;
        for (unsigned i = 0; i < 4; ++i) {
            uint8_t a = bits_at(bits,count,pos+i*16,8), b = bits_at(bits,count,pos+i*16+8,8);
            if ((a & 0xaa) != 0xaa || (b & 0xaa) != 0xaa) valid = false;
            header[i] = ((a << 1) | 1) & b;
        }
        if (!valid || header[1] != track || header[2] >= 16 ||
            physical_to_dos[header[2]] != sector ||
            (header[0] ^ header[1] ^ header[2]) != header[3] ||
            bits_at(bits,count,pos+64,16) != 0xdeaa) continue;
        pos += 88;
        uint32_t gap;
        for (gap = 0; gap < 512; ++gap) {
            uint32_t marker = bits_at(bits,count,pos+gap,24);
            if (marker == 0xd5aaad) break;
            if (marker == 0xd5aa96) { gap = 512; break; }
        }
        if (gap == 512) continue;
        pos += gap + 24;
        uint32_t limit = pos + 4096;
        uint8_t values[342], previous = 0;
        for (unsigned i = 0; i <= 342; ++i) {
            uint8_t nibble;
            if (!read_nibble(bits, count, &pos, limit, &nibble)) { valid = false; break; }
            uint8_t value = decode[nibble];
            if (value == 0xff) { valid = false; break; }
            value ^= previous;
            if (i < 342) values[i] = value;
            else if (value) valid = false;
            previous = value;
        }
        uint8_t tail1, tail2;
        if (!valid || !read_nibble(bits,count,&pos,limit,&tail1) ||
            !read_nibble(bits,count,&pos,limit,&tail2) || tail1 != 0xde || tail2 != 0xaa) continue;
        for (unsigned i = 0; i < 256; ++i) {
            uint8_t low = (values[i % 86] >> (2 * (i / 86))) & 3;
            out[i] = (values[86+i] << 2) | ((low & 1) << 1) | (low >> 1);
        }
        return true;
    }
    return false;
}
