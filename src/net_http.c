#include "net_http.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool bounded_copy(char *dst, size_t cap, const char *s, size_t n) {
    if (n >= cap) return false;
    memcpy(dst, s, n); dst[n] = 0; return true;
}

int nh_parse_url(const char *text, nh_url *out) {
    size_t total = strlen(text);
    if (total > NH_URL_MAX) return NH_URL_LONG;
    for (size_t i = 0; i < total; i++)
        if ((unsigned char)text[i] <= 32 || (unsigned char)text[i] >= 127 || text[i] == '\\')
            return NH_URL;
    const char *host;
    if (!strncasecmp(text, "https://", 8)) { out->tls = true; host = text + 8; }
    else if (!strncasecmp(text, "http://", 7)) { out->tls = false; host = text + 7; }
    else return NH_URL;
    out->port = out->tls ? 443 : 80;
    const char *end = host + strcspn(host, "/?#");
    const char *colon = memchr(host, ':', (size_t)(end - host));
    const char *host_end = colon ? colon : end;
    if (host == host_end || !bounded_copy(out->host, sizeof(out->host), host, host_end - host))
        return NH_URL;
    for (char *p = out->host; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '.') return NH_URL;
        *p = (char)tolower((unsigned char)*p);
    }
    if (colon) {
        unsigned port = 0;
        if (colon + 1 == end) return NH_URL;
        for (const char *p = colon + 1; p < end; p++) {
            if (!isdigit((unsigned char)*p) || port > 6553) return NH_URL;
            port = port * 10 + (*p - '0');
        }
        if (!port || port > 65535) return NH_URL;
        out->port = (uint16_t)port;
    }
    size_t n = strcspn(end, "#");
    if (*end == '/') {
        if (!bounded_copy(out->path, sizeof(out->path), end, n)) return NH_URL_LONG;
    } else {
        out->path[0] = '/';
        if (!bounded_copy(out->path + 1, sizeof(out->path) - 1, end, n)) return NH_URL_LONG;
    }
    return NH_OK;
}

/* RFC 3986 dot-segment removal, only for redirect resolution. Do not normalize
 * user paths: repeated slashes and percent-encoded bytes can be meaningful. */
static void remove_dots(char *path) {
    char result[NH_URL_MAX + 1];
    const char *p = path;
    size_t n = 0;
    while (*p && *p != '?') {
        if (!strncmp(p, "../", 3)) p += 3;
        else if (!strncmp(p, "./", 2)) p += 2;
        else if (!strncmp(p, "/./", 3)) p += 2;
        else if (!strcmp(p, "/.") || !strncmp(p, "/.?", 3)) { p += 2; result[n++] = '/'; }
        else if (!strncmp(p, "/../", 4) || !strcmp(p, "/..") || !strncmp(p, "/..?", 4)) {
            bool last = p[3] != '/'; p += 3;
            while (n && result[n - 1] != '/') n--;
            if (n) n--;
            if (last) result[n++] = '/';
        } else {
            if (*p == '/') result[n++] = *p++;
            while (*p && *p != '/' && *p != '?') result[n++] = *p++;
        }
    }
    strcpy(result + n, p);
    strcpy(path, result);
}

int nh_resolve(const nh_url *base, const char *location, nh_url *out) {
    char text[NH_URL_MAX + 1];
    int n;
    if (!strncasecmp(location, "http://", 7) || !strncasecmp(location, "https://", 8)) {
        int e = nh_parse_url(location, out);
        if (!e) remove_dots(out->path);
        return e;
    }
    if (!strncmp(location, "//", 2)) n = snprintf(text, sizeof(text), "%s:%s", base->tls ? "https" : "http", location);
    else {
        /* A colon in the first path segment denotes a scheme, not a relative URL. */
        size_t first = strcspn(location, "/?#");
        if (memchr(location, ':', first)) return NH_URL;
        n = snprintf(text, sizeof(text), "%s://%s:%u", base->tls ? "https" : "http", base->host, base->port);
        if (n < 0 || (size_t)n >= sizeof(text)) return NH_URL_LONG;
        int used = n;
        if (*location == '/') n = snprintf(text + used, sizeof(text) - used, "%s", location);
        else {
            size_t prefix = strcspn(base->path, "?");
            if (*location == '#' || !*location) prefix = strlen(base->path);
            else if (*location != '?') {
                while (prefix && base->path[prefix - 1] != '/') prefix--;
            }
            n = snprintf(text + used, sizeof(text) - used, "%.*s%s", (int)prefix, base->path, location);
        }
        if (n < 0 || (size_t)n >= sizeof(text) - used) return NH_URL_LONG;
        n += used;
    }
    if (n < 0 || (size_t)n >= sizeof(text)) return NH_URL_LONG;
    int e = nh_parse_url(text, out);
    if (!e) remove_dots(out->path);
    return e;
}

static bool header_name(const char *name, size_t size, const char *match) {
    return strlen(match) == size && !strncasecmp(name, match, size);
}

int nh_request(char *out, size_t cap, const nh_url *url, unsigned method,
        const char *headers, const uint8_t *body, size_t body_size) {
    static const char *methods[] = {"GET", "POST", "PUT", "PATCH", "DELETE"};
    if (method >= NH_METHOD_COUNT || (method == NH_GET && body_size)) return -NH_REQUEST;
    if (body_size > NH_REQUEST_BODY_MAX || strlen(headers) > NH_REQUEST_HEADERS_MAX) return -NH_TOO_LARGE;
    char authority[262];
    if (url->port == (url->tls ? 443 : 80)) snprintf(authority, sizeof(authority), "%s", url->host);
    else snprintf(authority, sizeof(authority), "%s:%u", url->host, url->port);
    int n = snprintf(out, cap,
        "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: frank-apple\r\n"
        "Accept-Encoding: identity\r\nConnection: close\r\n",
        methods[method], url->path, authority);
    if (n < 0 || (size_t)n >= cap) return -NH_TOO_LARGE;
    bool accept = false, content_type = false;
    const char *names[16]; size_t lengths[16], fields = 0;
    for (const char *p = headers; *p;) {
        const char *end = p;
        while (*end && *end != '\r' && *end != '\n') end++;
        const char *colon = memchr(p, ':', (size_t)(end-p));
        if (!colon || colon == p || fields == 16) return -NH_REQUEST;
        size_t name_size = (size_t)(colon-p);
        for (const char *c = p; c < colon; c++)
            if ((unsigned char)*c >= 127 || (!isalnum((unsigned char)*c) && !strchr("!#$%&'*+-.^_`|~", *c))) return -NH_REQUEST;
        static const char *owned[] = {"host", "content-length", "transfer-encoding", "connection",
            "accept-encoding", "user-agent", "expect", "upgrade", "trailer", "te"};
        for (size_t i = 0; i < sizeof(owned)/sizeof(*owned); i++)
            if (header_name(p, name_size, owned[i])) return -NH_REQUEST;
        for (size_t i = 0; i < fields; i++)
            if (lengths[i] == name_size && !strncasecmp(names[i], p, name_size)) return -NH_REQUEST;
        names[fields] = p; lengths[fields++] = name_size;
        accept |= header_name(p, name_size, "accept");
        content_type |= header_name(p, name_size, "content-type");
        const char *value = colon+1, *value_end = end;
        while (value < end && (*value == ' ' || *value == '\t')) value++;
        while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) value_end--;
        for (const char *c = value; c < value_end; c++)
            if (((unsigned char)*c < 32 && *c != '\t') || (unsigned char)*c >= 127) return -NH_REQUEST;
        int k = snprintf(out+n, cap-(size_t)n, "%.*s: %.*s\r\n", (int)name_size,p,(int)(value_end-value),value);
        if (k < 0 || (size_t)k >= cap-(size_t)n) return -NH_TOO_LARGE;
        n += k;
        p = end;
        if (*p == '\r') { p++; if (*p == '\n') p++; }
        else if (*p == '\n') p++;
    }
    int k = snprintf(out+n, cap-(size_t)n, "%s%s",
        accept ? "" : "Accept: */*\r\n",
        body_size && !content_type ? "Content-Type: application/octet-stream\r\n" : "");
    if (k < 0 || (size_t)k >= cap-(size_t)n) return -NH_TOO_LARGE;
    n += k;
    if (method != NH_GET) {
        k = snprintf(out+n, cap-(size_t)n, "Content-Length: %u\r\n", (unsigned)body_size);
        if (k < 0 || (size_t)k >= cap-(size_t)n) return -NH_TOO_LARGE;
        n += k;
    }
    if ((size_t)n + 2 + body_size >= cap) return -NH_TOO_LARGE;
    memcpy(out+n, "\r\n", 2); n += 2;
    if (body_size) memcpy(out+n, body, body_size);
    n += (int)body_size; out[n] = 0;
    return n;
}

bool nh_is_redirect(unsigned s) { return s == 301 || s == 302 || s == 303 || s == 307 || s == 308; }
static void fail(nh_response *r, int e) { r->error = (uint8_t)e; r->phase = NH_FAILED; }
void nh_init(nh_response *r) { memset(r, 0, sizeof(*r)); r->phase = NH_STATUS; r->redirect_headers_only = true; }

static bool number(const char *s, unsigned base, uint32_t *out) {
    uint32_t n = 0;
    if (!*s) return false;
    for (; *s; s++) {
        int d = isdigit((unsigned char)*s) ? *s - '0' :
            (tolower((unsigned char)*s) >= 'a' && tolower((unsigned char)*s) <= 'f' ? tolower((unsigned char)*s) - 'a' + 10 : -1);
        if (d < 0 || (unsigned)d >= base || n > (UINT32_MAX - d) / base) return false;
        n = n * base + d;
    }
    *out = n; return true;
}

static void header(nh_response *r) {
    char *colon = strchr(r->line, ':');
    if (!colon || colon == r->line) { fail(r, NH_PROTOCOL); return; }
    for (char *p = r->line; p < colon; p++)
        if (!isalnum((unsigned char)*p) && !strchr("!#$%&'*+-.^_`|~", *p)) { fail(r, NH_PROTOCOL); return; }
    *colon++ = 0;
    while (*colon == ' ' || *colon == '\t') colon++;
    char *end = colon + strlen(colon);
    while (end > colon && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    if (r->phase == NH_TRAILERS) return; /* trailers never change framing */
    if (!strcasecmp(r->line, "content-length")) {
        uint32_t len;
        if (!number(colon, 10, &len) || (r->have_length && len != r->length)) fail(r, NH_PROTOCOL);
        else { r->have_length = true; r->length = len; }
    } else if (!strcasecmp(r->line, "transfer-encoding")) {
        if (r->chunked || strcasecmp(colon, "chunked")) fail(r, NH_UNSUPPORTED);
        else r->chunked = true;
    } else if (!strcasecmp(r->line, "content-encoding")) {
        if (r->have_encoding || strcasecmp(colon, "identity")) fail(r, NH_UNSUPPORTED);
        r->have_encoding = true;
    } else if (!strcasecmp(r->line, "location")) {
        if (*r->location || !bounded_copy(r->location, sizeof(r->location), colon, strlen(colon))) fail(r, NH_PROTOCOL);
    }
}

static void line(nh_response *r) {
    if (r->phase == NH_STATUS) {
        if (r->line_len < 12 || strncmp(r->line, "HTTP/1.", 7) ||
            (r->line[7] != '0' && r->line[7] != '1') || r->line[8] != ' ' ||
            !isdigit((unsigned char)r->line[9]) || !isdigit((unsigned char)r->line[10]) ||
            !isdigit((unsigned char)r->line[11]) || (r->line[12] && r->line[12] != ' ')) {
            fail(r, NH_PROTOCOL); return;
        }
        r->status = (r->line[9] - '0') * 100 + (r->line[10] - '0') * 10 + r->line[11] - '0';
        if (r->status < 100 || r->status > 599 || r->status == 101) fail(r, NH_PROTOCOL);
        else r->phase = NH_HEADERS;
    } else if (r->phase == NH_CHUNK_SIZE) {
        char *extension = strchr(r->line, ';');
        if (extension) *extension = 0;
        if (!number(r->line, 16, &r->left)) fail(r, NH_PROTOCOL);
        else if (r->left > NH_BODY_MAX - r->size) fail(r, NH_TOO_LARGE);
        else r->phase = r->left ? NH_CHUNK_DATA : NH_TRAILERS;
    } else if (r->line_len) header(r);
    else if (r->phase == NH_TRAILERS) r->phase = NH_DONE;
    else if (r->status < 200) {
        if (++r->interim > 3 || r->have_length || r->chunked) { fail(r, NH_PROTOCOL); return; }
        r->status = 0; r->have_encoding = false; r->location[0] = 0; r->phase = NH_STATUS;
    } else if (r->status == 204 || r->status == 304 || (r->redirect_headers_only && nh_is_redirect(r->status) && *r->location)) {
        /* Redirects need headers only; the old connection is closed before following. */
        r->phase = NH_DONE;
    } else if (r->chunked && r->have_length) fail(r, NH_PROTOCOL);
    else if (r->chunked) r->phase = NH_CHUNK_SIZE;
    else if (r->have_length) {
        r->left = r->length;
        if (r->left > NH_BODY_MAX) fail(r, NH_TOO_LARGE);
        else r->phase = r->left ? NH_FIXED : NH_DONE;
    } else r->phase = NH_CLOSE;
}

void nh_feed(nh_response *r, const uint8_t *bytes, size_t size) {
    for (size_t i = 0; i < size && r->phase != NH_FAILED && r->phase != NH_DONE; i++) {
        uint8_t c = bytes[i];
        if (r->phase == NH_FIXED || r->phase == NH_CLOSE || r->phase == NH_CHUNK_DATA) {
            if (r->size == NH_BODY_MAX) { fail(r, NH_TOO_LARGE); return; }
            r->body[r->size++] = c;
            if (r->phase != NH_CLOSE && --r->left == 0) r->phase = r->phase == NH_FIXED ? NH_DONE : NH_CHUNK_CR;
        } else if (r->phase == NH_CHUNK_CR) {
            if (c != '\r') fail(r, NH_PROTOCOL); else r->phase = NH_CHUNK_LF;
        } else if (r->phase == NH_CHUNK_LF) {
            if (c != '\n') fail(r, NH_PROTOCOL); else r->phase = NH_CHUNK_SIZE;
        } else {
            if (r->phase != NH_CHUNK_SIZE && ++r->header_bytes > NH_HEADER_MAX) { fail(r, NH_TOO_LARGE); return; }
            if (r->cr) {
                r->cr = false;
                if (c != '\n') { fail(r, NH_PROTOCOL); return; }
                r->line[r->line_len] = 0; line(r); r->line_len = 0;
            } else if (c == '\r') r->cr = true;
            else if (c == '\n' || !c || (c < 32 && c != '\t') || c == 127) fail(r, NH_PROTOCOL);
            else if (r->line_len == NH_LINE_MAX) fail(r, NH_TOO_LARGE);
            else r->line[r->line_len++] = (char)c;
        }
    }
}

void nh_eof(nh_response *r) {
    if (r->phase == NH_CLOSE) r->phase = NH_DONE;
    else if (r->phase != NH_DONE && r->phase != NH_FAILED) fail(r, NH_PROTOCOL);
}
