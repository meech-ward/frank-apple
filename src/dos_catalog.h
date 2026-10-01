// SPDX-License-Identifier: MIT
// Read-only DOS 3.3 catalog support for the button launcher.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DOS_PROGRAM_MAX 105
typedef struct { char name[31]; } dos_program_t;
typedef bool (*dos_sector_reader)(void *, uint8_t, uint8_t, uint8_t[256]);
// Returns a count, or -1 for unsupported/damaged media. Never returns a partial list.
int dos_catalog_scan(dos_sector_reader read, void *context,
                     dos_program_t *programs, size_t capacity);
// Decode a DOS logical sector from a circular, MSB-first 16-sector GCR track.
bool dos_catalog_sector(const uint8_t *bits, uint32_t count, uint8_t track,
                        uint8_t sector, uint8_t out[256]);
// Reject names that cannot be represented as a single DOS keyboard command.
bool dos_program_command(char *out, size_t capacity, const char *name, bool run);
