#pragma once
#include <stdbool.h>
#include <stddef.h>

#define WIFI_CONFIG_MAX 512
typedef struct { char ssid[33]; char password[65]; } wifi_config;
enum wifi_config_status { WIFI_CONFIG_MISSING, WIFI_CONFIG_READY, WIFI_CONFIG_INVALID, WIFI_CONFIG_IO };

/* Parse a bounded INI file; failure clears the entire result. No credentials
 * are logged or compiled into the firmware. */
bool wifi_config_parse(const char *text, size_t size, wifi_config *out);
enum wifi_config_status wifi_config_load(wifi_config *out);
