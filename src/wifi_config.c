#include "wifi_config.h"
#include <string.h>
#include <ctype.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    return s;
}

bool wifi_config_parse(const char *text, size_t size, wifi_config *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!text || size > WIFI_CONFIG_MAX || memchr(text, 0, size)) return false;
    wifi_config config = {0};
    enum { NONE, WIFI, SSH } section = NONE;
    bool have_wifi = false, have_ssh = false;
    bool have_ssid = false, have_password = false, have_mode = false;
    bool have_ssh_enabled = false, have_ssh_password = false;
    size_t pos = size >= 3 && !memcmp(text, "\xef\xbb\xbf", 3) ? 3 : 0;
    while (pos < size) {
        char line[128]; size_t n = 0;
        while (pos < size && text[pos] != '\r' && text[pos] != '\n') {
            unsigned char c = (unsigned char)text[pos++];
            if (n == sizeof(line)-1 || (c < 32 && c != '\t') || c == 127) return false;
            line[n++] = (char)c;
        }
        if (pos < size && text[pos++] == '\r' && pos < size && text[pos] == '\n') pos++;
        line[n] = 0;
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        if (!strcmp(s, "[wifi]")) {
            if (have_wifi) return false;
            have_wifi = true; section = WIFI; continue;
        }
        if (!strcmp(s, "[ssh]")) {
            if (have_ssh) return false;
            have_ssh = true; section = SSH; continue;
        }
        if (section == NONE) return false;
        char *eq = strchr(s, '=');
        if (!eq) return false;
        *eq++ = 0;
        char *key = trim(s), *value = trim(eq);
        size_t len = strlen(value);
        /* Optional matching quotes preserve leading/trailing spaces. Backslashes
         * and #/; inside values are literal, with no escape or inline-comment rules. */
        if (len && (*value == '"' || *value == '\'')) {
            if (len < 2 || value[len-1] != *value) return false;
            value++; len -= 2; value[len] = 0;
        }
        for (size_t i = 0; i < len; i++) if ((unsigned char)value[i] < 32 || value[i] == 127) return false;
        if (section == SSH) {
            if (!strcmp(key, "enabled")) {
                if (have_ssh_enabled) return false;
                if (!strcmp(value, "true")) config.ssh_enabled = true;
                else if (strcmp(value, "false")) return false;
                have_ssh_enabled = true;
            } else if (!strcmp(key, "password")) {
                if (have_ssh_password || len < 8 || len > 64) return false;
                memcpy(config.ssh_password, value, len+1);
                have_ssh_password = true;
            } else return false;
        } else if (!strcmp(key, "mode")) {
            if (have_mode) return false;
            if (!strcmp(value, "station")) config.mode = WIFI_MODE_STATION;
            else if (!strcmp(value, "hotspot")) config.mode = WIFI_MODE_HOTSPOT;
            else return false;
            have_mode = true;
        } else if (!strcmp(key, "ssid")) {
            if (have_ssid || !len || len > 32) return false;
            memcpy(config.ssid, value, len+1); have_ssid = true;
        } else if (!strcmp(key, "password")) {
            if (have_password || len > 64 || (len && len < 8)) return false;
            if (len == 64) for (size_t i = 0; i < len; i++) if (!isxdigit((unsigned char)value[i])) return false;
            memcpy(config.password, value, len+1); have_password = true;
        } else return false;
    }
    if (!have_wifi || !have_ssid || !have_password) return false;
    /* Portable mode is always a secured WPA2 access point. A passphrase,
     * not an open network or raw 64-digit PSK, keeps client setup portable. */
    if (config.mode == WIFI_MODE_HOTSPOT &&
            (strlen(config.password) < 8 || strlen(config.password) > 63)) return false;
    if (config.ssh_enabled && !have_ssh_password) return false;
    *out = config;
    return true;
}
