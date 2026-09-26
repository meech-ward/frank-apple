#include "wifi_config.h"
#include "ff.h"
#include <string.h>

enum wifi_config_status wifi_config_load(wifi_config *out) {
    FIL file; UINT count = 0;
    char text[WIFI_CONFIG_MAX + 1];
    memset(out, 0, sizeof(*out));
    FRESULT result = f_open(&file, "/wifi.ini", FA_READ);
    if (result == FR_NO_FILE || result == FR_NO_PATH || result == FR_NOT_READY || result == FR_NOT_ENABLED)
        return WIFI_CONFIG_MISSING;
    if (result != FR_OK) return WIFI_CONFIG_IO;
    result = f_read(&file, text, sizeof(text), &count);
    FRESULT close_result = f_close(&file);
    if (result != FR_OK || close_result != FR_OK) return WIFI_CONFIG_IO;
    return wifi_config_parse(text, count, out) ? WIFI_CONFIG_READY : WIFI_CONFIG_INVALID;
}
