/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Load a stable private host seed, creating it once if absent. The random
 * callback must fill all requested bytes from the platform entropy source.
 * Storage failure disables SSH; an existing invalid key is never replaced. */
bool ssh_identity_load(uint8_t seed[32], bool (*random_bytes)(uint8_t *, size_t));
const char *ssh_identity_error(void);
