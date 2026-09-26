#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NH_URL_MAX 511
#define NH_BODY_MAX 4096
#define NH_HEADER_MAX 4096
#define NH_LINE_MAX 1023
#define NH_REQUEST_HEADERS_MAX 1024
#define NH_REQUEST_BODY_MAX 1024
#define NH_REQUEST_MAX 4096

enum nh_method { NH_GET, NH_POST, NH_PUT, NH_PATCH, NH_DELETE, NH_METHOD_COUNT };

enum nh_error {
    NH_OK, NH_WIFI, NH_URL, NH_DNS, NH_CONNECT, NH_TIMEOUT, NH_PROTOCOL,
    NH_TOO_LARGE, NH_REDIRECT, NH_CANCELLED, NH_TLS, NH_URL_LONG,
    NH_UNSUPPORTED, NH_HTTP_STATUS, NH_REQUEST
};
typedef struct {
    bool tls;
    uint16_t port;
    char host[254];
    char path[NH_URL_MAX + 1];
} nh_url;

enum nh_phase { NH_STATUS, NH_HEADERS, NH_FIXED, NH_CLOSE, NH_CHUNK_SIZE,
    NH_CHUNK_DATA, NH_CHUNK_CR, NH_CHUNK_LF, NH_TRAILERS, NH_DONE, NH_FAILED };
typedef struct {
    enum nh_phase phase;
    uint8_t error, interim;
    uint16_t status, line_len, header_bytes;
    bool cr, have_length, chunked, have_encoding, redirect_headers_only;
    uint32_t length, left;
    size_t size;
    char line[NH_LINE_MAX + 1];
    char location[NH_URL_MAX + 1];
    uint8_t body[NH_BODY_MAX];
} nh_response;

int nh_parse_url(const char *text, nh_url *out);
int nh_resolve(const nh_url *base, const char *location, nh_url *out);
bool nh_is_redirect(unsigned status);
/* The caller supplies all headers explicitly. Returns bytes or a negative
 * nh_error. Framing/transport headers are owned by this serializer. */
int nh_request(char *out, size_t cap, const nh_url *url, unsigned method,
        const char *headers, const uint8_t *body, size_t body_size);
void nh_init(nh_response *r);
void nh_feed(nh_response *r, const uint8_t *bytes, size_t size);
void nh_eof(nh_response *r);
