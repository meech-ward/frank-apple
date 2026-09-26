#pragma once

#include <stdbool.h>

void netcard_init(void);
void netcard_poll(void);
void netcard_cancel(void);

// Human-readable Wi-Fi setup/connection state for the disk menu.
const char *netcard_wifi_status(void);
