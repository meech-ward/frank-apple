#pragma once
#include <stdbool.h>
#include <stddef.h>

/* All entry points and lwIP callbacks run on core 0. Disabled after every reset. */
bool web_control_enabled(void);
void web_control_toggle(void);
void web_control_poll(void);
void web_control_address(char *out, size_t cap);
