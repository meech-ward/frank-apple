// Original downstream disk import support. MIT; see LICENSE-DOWNSTREAM.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LIBRARY_CHUNK 1024u
#define LIBRARY_MAX_FILE (1024u * 1024u)
#define LIBRARY_NAME_MAX 58u
bool library_name_valid(const char *name);
// Return HTTP status; reply is JSON on success, explanatory text on error.
int library_begin(const char *name, uint32_t size, char *reply, size_t cap);
int library_chunk(uint32_t id, uint32_t offset, const uint8_t *data, size_t size, char *reply, size_t cap);
int library_finish(uint32_t id, char *reply, size_t cap);
int library_cancel(uint32_t id, char *reply, size_t cap);
int library_list(unsigned offset, char *reply, size_t cap);
bool library_uploading(void);
void library_poll(void);
void library_shutdown(void);
