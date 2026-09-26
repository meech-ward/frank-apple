#include "net_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static nh_response response;
static void parse(const char *s, size_t stride) {
    nh_init(&response);
    for (size_t i = 0, n = strlen(s); i < n; i += stride)
        nh_feed(&response, (const uint8_t *)s + i, n - i < stride ? n - i : stride);
    nh_eof(&response);
}
static void expect(const char *s, unsigned status, const char *body) {
    for (size_t step = 1; step < strlen(s) + 1; step++) {
        parse(s, step);
        assert(response.phase == NH_DONE && !response.error);
        assert(response.status == status && response.size == strlen(body));
        assert(!memcmp(response.body, body, response.size));
    }
}
static void bad(const char *s, int error) {
    parse(s, 1); assert(response.phase == NH_FAILED && response.error == error);
}
int main(void) {
    nh_url a, b;
    assert(!nh_parse_url("HtTpS://HTTPBIN.org:443/get?x=a%20b#fragment", &a));
    assert(a.tls && a.port == 443 && !strcmp(a.host, "httpbin.org") && !strcmp(a.path, "/get?x=a%20b"));
    assert(!nh_parse_url("http://10.0.0.20:8000?x=1", &b));
    assert(!b.tls && b.port == 8000 && !strcmp(b.path, "/?x=1"));
    const char *invalid[] = {"", "ftp://x/a", "https://user:pw@host/a", "http://a:0", "http://a:65536", "http://a:99999999999999", "http://a:", "http://a:4x", "http://[::1]/", "http://a/\r\nX:1", "http://a/a b", "http://a\\b", "http:///a"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); i++) assert(nh_parse_url(invalid[i], &b));
    char big[1200]; memset(big, 'a', sizeof(big)); big[sizeof(big)-1]=0;
    assert(nh_parse_url(big, &b) == NH_URL_LONG);
    assert(!nh_parse_url("https://example.org/a/b/c?old=1", &a));
    struct { const char *location, *path, *host; bool tls; unsigned port; } redirects[] = {
        {"../d?q=2", "/a/d?q=2", "example.org", true, 443},
        {"/foo/./bar/../", "/foo/", "example.org", true, 443},
        {"?new=1", "/a/b/c?new=1", "example.org", true, 443},
        {"#fragment", "/a/b/c?old=1", "example.org", true, 443},
        {"../../../../z", "/z", "example.org", true, 443},
        {".", "/a/b/", "example.org", true, 443},
        {"..", "/a/", "example.org", true, 443},
        {"//other.org:8080/x", "/x", "other.org", true, 8080},
        {"http://other.org/x", "/x", "other.org", false, 80},
    };
    for (size_t i = 0; i < sizeof(redirects)/sizeof(*redirects); i++) {
        assert(!nh_resolve(&a, redirects[i].location, &b));
        assert(!strcmp(b.path, redirects[i].path) && !strcmp(b.host, redirects[i].host));
        assert(b.tls == redirects[i].tls && b.port == redirects[i].port);
    }
    assert(nh_resolve(&a, "file:/etc/passwd", &b));
    assert(nh_resolve(&a, big, &b));
    b = a;
    assert(!nh_resolve(&b, "../same-object", &b) && !strcmp(b.path, "/a/same-object"));
    char request[NH_REQUEST_MAX];
    assert(nh_request(request, sizeof(request), &a, NH_GET, "", NULL, 0) > 0);
    assert(!strstr(request, "Authorization"));
    assert(strstr(request, "GET /a/b/c?old=1 HTTP/1.1\r\n") && strstr(request, "Accept-Encoding: identity\r\n"));
    assert(nh_request(request, sizeof(request), &a, NH_GET, "Accept: text/csv\rapikey: PUBLIC_FIXTURE\rAuthorization: Bearer USER_FIXTURE\r", NULL, 0) > 0);
    assert(strstr(request,"Accept: text/csv\r\n") && !strstr(request,"Accept: */*"));
    assert(strstr(request,"apikey: PUBLIC_FIXTURE\r\n") && strstr(request,"Authorization: Bearer USER_FIXTURE\r\n"));
    const uint8_t raw[] = {'{',0,128,'}'};
    const char *verbs[] = {"GET", "POST", "PUT", "PATCH", "DELETE"};
    for (unsigned m = NH_POST; m < NH_METHOD_COUNT; m++) {
        int n = nh_request(request,sizeof(request),&a,m,"Content-Type: application/json\nPrefer: return=representation\n",raw,sizeof(raw));
        assert(n > 0 && !strncmp(request,verbs[m],strlen(verbs[m])));
        assert(strstr(request,"Content-Length: 4\r\n"));
        assert(!memcmp(request+n-sizeof(raw),raw,sizeof(raw)));
    }
    assert(nh_request(request, sizeof(request), &a, NH_GET, "", raw, 1) == -NH_REQUEST);
    assert(nh_request(request, sizeof(request), &a, NH_METHOD_COUNT, "", NULL, 0) == -NH_REQUEST);
    assert(nh_request(request, 20, &a, NH_GET, "", NULL, 0) == -NH_TOO_LARGE);
    const char *bad_headers[] = {"Host: other", "Content-Length: 2", "Connection: upgrade", "Transfer-Encoding: chunked",
        "Accept-Encoding: gzip", "Expect: 100-continue", "Upgrade: websocket", "Trailer: x", "TE: trailers", "User-Agent: other",
        "X: a\r\n\r\nGET / HTTP/1.1", " X: folded", "X : spaced", "No-colon", "X: a\nX: b", "X: a\nx: b", "X: a\001b"};
    for (size_t i=0;i<sizeof(bad_headers)/sizeof(*bad_headers);i++)
        assert(nh_request(request,sizeof(request),&a,NH_GET,bad_headers[i],NULL,0)==-NH_REQUEST);
    uint8_t body[NH_REQUEST_BODY_MAX+1];memset(body,'b',sizeof(body));
    assert(nh_request(request,sizeof(request),&a,NH_POST,"",body,NH_REQUEST_BODY_MAX)>0);
    assert(strstr(request,"Content-Type: application/octet-stream\r\n"));
    assert(nh_request(request,sizeof(request),&a,NH_POST,"",body,sizeof(body))==-NH_TOO_LARGE);
    memset(big,'a',sizeof(big));big[0]='X';big[1]=':';big[sizeof(big)-1]=0;
    assert(nh_request(request,sizeof(request),&a,NH_GET,big,NULL,0)==-NH_TOO_LARGE);
    big[NH_REQUEST_HEADERS_MAX]=0;
    assert(nh_request(request,sizeof(request),&a,NH_GET,big,NULL,0)>0);
    /* Redirects returned to the caller must retain their actual response body. */
    nh_init(&response); response.redirect_headers_only=false;
    const char *redirect_body="HTTP/1.1 307 Temporary Redirect\r\nLocation: /other\r\nContent-Length: 4\r\n\r\nWAIT";
    nh_feed(&response,(const uint8_t *)redirect_body,strlen(redirect_body));nh_eof(&response);
    assert(response.phase==NH_DONE && response.size==4 && !memcmp(response.body,"WAIT",4));
    expect("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", 200, "hello");
    expect("HTTP/1.0 404 Missing\r\n\r\nnot here", 404, "not here");
    expect("HTTP/1.1 204 No Content\r\n\r\n", 204, "");
    expect("HTTP/1.1 304 Unchanged\r\nContent-Length: 99999\r\n\r\n", 304, "");
    expect("HTTP/1.1 302 Found\r\nLocation: /uuid\r\nContent-Length: 99999\r\n\r\n", 302, "");
    expect("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\nOK", 200, "OK");
    expect("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2;ext=yes\r\nhe\r\n3\r\nllo\r\n0\r\nX-Trailer: ok\r\n\r\n", 200, "hello");
    expect("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", 200, "");
    bad("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nx", NH_PROTOCOL);
    bad("HTTP/1.1 200 OK\r\nContent-Length: 4097\r\n\r\n", NH_TOO_LARGE);
    bad("HTTP/1.1 200 OK\r\nContent-Length: 9999999999999\r\n\r\n", NH_PROTOCOL);
    bad("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\n", NH_PROTOCOL);
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 2\r\n\r\n", NH_PROTOCOL);
    bad("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n", NH_UNSUPPORTED);
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n", NH_UNSUPPORTED);
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1001\r\n", NH_TOO_LARGE);
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n", NH_PROTOCOL);
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nxX", NH_PROTOCOL);
    bad("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n", NH_PROTOCOL);
    bad("HTTP/1.1 200 OK\r\nBroken header\r\n\r\n", NH_PROTOCOL);
    bad("HTTP/1.1 200 OK\n\n", NH_PROTOCOL);
    bad("HTTP/1.1 999 Bad\r\n\r\n", NH_PROTOCOL);
    bad("HTTP/1.1 101 Switch\r\n\r\n", NH_PROTOCOL);
    /* Exact-size and binary responses, including NUL and the high bit. */
    nh_init(&response);
    const char *head = "HTTP/1.1 200 OK\r\nContent-Length: 4096\r\n\r\n";
    nh_feed(&response, (const uint8_t *)head, strlen(head));
    for (unsigned i = 0; i < NH_BODY_MAX; i++) { uint8_t c = i; nh_feed(&response, &c, 1); }
    assert(response.phase == NH_DONE && response.size == NH_BODY_MAX);
    for (unsigned i = 0; i < NH_BODY_MAX; i++) assert(response.body[i] == (uint8_t)i);
    nh_init(&response); head = "HTTP/1.1 200 OK\r\n\r\n";
    nh_feed(&response, (const uint8_t *)head, strlen(head));
    for (unsigned i = 0; i <= NH_BODY_MAX; i++) { uint8_t c = 'x'; nh_feed(&response, &c, 1); }
    assert(response.error == NH_TOO_LARGE);
    nh_init(&response); head = "HTTP/1.1 200 OK\r\nX: ";
    nh_feed(&response, (const uint8_t *)head, strlen(head));
    nh_feed(&response, (const uint8_t *)big, strlen(big));
    assert(response.error == NH_TOO_LARGE);
    /* Malformed-input smoke fuzz under ASan/UBSan. */
    unsigned seed = 1;
    for (int k = 0; k < 1000; k++) {
        nh_init(&response);
        for (size_t i = 0; i < sizeof(big); i++) { seed = seed * 1664525u + 1013904223u; big[i] = (char)(seed >> 24); }
        nh_feed(&response, (const uint8_t *)big, sizeof(big)); nh_eof(&response);
    }
    puts("PASS: methods, custom headers, framing ownership, binary request bodies, redirects, split HTTP responses, bounds and malformed input.");
}
