# HTTP requests from Applesoft BASIC

The badge's virtual slot-1 network card accepts a URL from your program and
fetches it directly over Wi-Fi. The computer and browser only supply keystrokes.
This is a modern add-on to the emulated Apple II; stock Applesoft has no HTTP command.

The public build uses `wifi.ini`; see [Wi-Fi setup](WIFI-SETUP.md). It has no
built-in Supabase connection. Original GET programs remain compatible.

## Send data to an API

Select **HTTP POST / API starter** in the browser's program editor and enter it.
You can also paste [the source](../basic/http-api-demo.bas), then `SAVE HTTP API`
on your DOS disk to load it again later. `RUN` sends a JSON message to
`https://httpbin.org/anything` and prints the server's echo. This test service
does not save the message. `LIST 10,100` shows your application; lines 9000 onward
are reusable networking helpers.

The application supplies:

- `U$`: the URL, including any query/filter parameters.
- `M`: 0 GET, 1 POST, 2 PUT, 3 PATCH, or 4 DELETE.
- `H$`: request headers, separated by `CHR$(13)`; empty means defaults.
- `D$`: request body; empty for GET. JSON, form data, CSV, or plain text are fine
  when the `Content-Type` header matches.
- `GOSUB 9000`: send and wait. `H` is the HTTP status, `E` the transfer error,
  and `GOSUB 9100` reads response chunks into `B$`, just as in the GET example.

For example, with the HTTP API helpers already loaded:

```basic
20 U$="https://httpbin.org/anything"
30 M=1:H$="Content-Type: text/plain"
50 D$="A MESSAGE FROM MY APPLE II"
RUN
```

Change `M` to 2, 3, or 4 to try PUT, PATCH, or DELETE against this echo endpoint.
For GET, set `M=0` and `D$=""`. To send multiple headers:

```basic
H$="Content-Type: application/json"+CHR$(13)+"Accept: application/json"
```

Custom headers such as `apikey`, `Authorization`, and `Prefer` can be supplied
explicitly by the program. No credentials are added automatically. With a
PostgREST server, URL parameters select/filter rows; POST inserts, PATCH updates,
and DELETE removes rows, subject to that server's permissions. A POST with
`Prefer: resolution=merge-duplicates` can upsert. You supply the endpoint, data,
and authentication appropriate to your API. There is no account setup or login
manager in the firmware.

The helpers accept Applesoft strings of up to 255 characters each. The card
itself accepts up to 1024 bytes each of custom headers and body; machine-code
callers or BASIC callers using repeated register writes can append more than
one string. Response bodies remain limited to 4096 bytes. This is a byte/text
interface, not a JSON parser.

Only GET **without custom headers** automatically follows redirects. Requests
with custom headers, and all POST/PUT/PATCH/DELETE requests, return their 3xx
status and response body directly. They are not silently repeated at another
URL. Failed/ambiguous writes are not retried automatically; the caller decides
what to do. A timeout does not prove that the server made no change.

The firmware owns `Host`, `Content-Length`, `Connection`, `Transfer-Encoding`,
`Accept-Encoding`, `User-Agent`, `Expect`, `Upgrade`, `Trailer`, and `TE`.
Supplying them, duplicate header names, invalid header lines, an unsupported
method, or a GET body produces error 14 before connecting. Up to 16 custom
headers are accepted. CR, LF, and CRLF separate header lines; blank separator
lines and folded headers are rejected. Request data may contain any byte.

## Try GET

Boot your DOS disk, enter the browser's **HTTP GET starter** (or paste
[the source](../basic/http-demo.bas)), then `RUN`. The starter fetches
`https://httpbin.org/uuid`, prints the HTTP status and displays the JSON body.
The public [httpbin service](https://httpbin.org/) requires no API key.

`LIST 10,90` shows the small application. The reusable helper routines begin at
line 9000. Change just line 20, then `RUN` again:

```basic
20 U$="https://httpbin.org/get?message=HELLO_APPLE_II"
RUN
```

The browser preserves case inside quoted BASIC strings. URL paths and query
values are case-sensitive. The command box recognizes the BASIC prompt, and the
program editor preserves quoted strings even during program entry. Outside
BASIC, use the existing **Preserve upper and lowercase** option for exact typing.

Other URLs to try:

- `https://httpbin.org/redirect/1` — follows a redirect to the request echo.
- `https://httpbin.org/status/404` — returns HTTP 404, with an empty body.
- `https://httpbin.org/stream/2` — decodes a chunked response.
- `http://httpbin.org/uuid` — unencrypted HTTP, using the same program.

The web page also has an **HTTP GET starter** button under **Write or paste your
own BASIC program**. It fills the editor only; entering it uses the existing
replacement confirmation. Source: `basic/http-demo.bas`.

## Helper interface

- `U$`: URL to fetch, up to Applesoft's 255-character string limit.
- `GOSUB 9000`: reset/start the request and wait. `H` is its final HTTP status;
  `E=0` means the transfer completed. `E$` describes a transfer error.
- `GOSUB 9100`: read at most 240 body bytes. `N=0` means EOF. `B$` contains text,
  converting LF and CRLF into the Apple's carriage-return newline. This is a
  text helper, not a JSON parser or Unicode decoder.
- HTTP 404/500 are completed HTTP responses: inspect `H`. Their bodies remain
  available. Connection/protocol failures set `E` instead.
- Helper scratch variables are `ZI`, `ZC` and `ZW`. Avoid using these in your
  own program. Put `END` before the helper routines to prevent falling into them.
- Stop/Ctrl-C cancels an in-flight GET as well as interrupting BASIC. Starting
  another request through the helper resets the old result.

## Limits and behavior

- HTTP/1.x GET, POST, PUT, PATCH and DELETE; HTTP and TLS 1.2 HTTPS; DNS/IPv4 hosts and explicit
  ports. IPv6 literals, URL userinfo, raw spaces/control characters and non-ASCII
  URLs are rejected. Percent-encode spaces or non-ASCII URL components.
- One request at a time, a 30-second overall deadline including DNS and redirects,
  and at most three redirect hops. Absolute, scheme-relative, root-relative and
  path-relative Location values are supported.
- 4096 body bytes after transfer decoding; 4096 header/trailer bytes and 1023
  bytes per line. Excess data produces an error rather than silent truncation.
- Content-Length, connection-close and chunked framing are supported. Requests
  ask for identity encoding; gzip and other compressed responses are rejected.
- Raw bytes are preserved by the card, including NUL and bytes above 127. Use
  the remaining-byte registers to detect EOF; zero can be valid body data.
- HTTPS requires certificate-chain and hostname verification using the bundled
  Google, Let's Encrypt and Amazon roots. This is a limited embedded trust store,
  not the complete browser CA store. The current platform has no trusted wall
  clock and does not enable certificate date checks. TLS 1.3-only servers are
  unsupported. No fallback to unverified HTTPS is made for URL GETs.
- Requests send only headers explicitly provided by the program. The old register-0
  demo now fetches `https://httpbin.org/uuid` without authentication. The former
  built-in Supabase connection and Realtime keyboard are excluded from the build.

## Card registers

Slot 1 base is `$C090` / 49296. The existing offsets 0–5 are compatible with
`basic/netdemo.bas` and Badge Desk. The original API version stays 1 so existing saved GET programs still run.
Read offset 15 for HTTP extension revision 2.

| Address | Read | Write |
| --- | --- | --- |
| 49296 | Wi-Fi link: 0 down, 1 up | Start the public UUID demo GET |
| 49297 | 0 idle, 1 fetching, 2 ready, 3 error | Ignored |
| 49298 | Next byte with legacy 7-bit/newline conversion | Rewind body reader |
| 49299–49300 | Unread body length, low/high bytes | Ignored |
| 49301 | Reserved: returns 0 | Ignored |
| 49302 | API version (1) | 0 reset all request fields; 1 send staged request; 2 cancel |
| 49303 | Next raw body byte | Append a URL byte |
| 49304–49305 | Final HTTP status, low/high bytes | Ignored |
| 49306 | Error code | Ignored |
| 49307–49308 | Staged URL length, low/high bytes | Ignored |
| 49309 | Selected method (0–4) | Select GET/POST/PUT/PATCH/DELETE (0–4) |
| 49310 | 0 (reserved) | Append one custom-header byte |
| 49311 | HTTP extension revision (2) | Append one raw body byte |

The URL register accepts up to 511 bytes, allowing callers to append multiple
strings. Reset before appending; this also clears headers/body and selects GET. While fetching, URL writes and extra starts are
ignored; reset/cancel are always accepted. A completed/error request does not
clear the staged URL, so sending again repeats it. Result bytes are readable
only in READY; a failed transfer never exposes its partial body as a success.

Error codes: 0 none, 1 Wi-Fi down, 2 bad URL, 3 DNS failure, 4 connection failure,
5 timeout, 6 malformed/incomplete HTTP, 7 response/request too large, 8 redirect
limit, 9 cancelled, 10 TLS/connection verification failure, 11 URL too long,
12 unsupported format, 13 non-200 status for the register-0 demo only,
14 invalid method/headers/body.

## Validation

See [BUILDING.md](BUILDING.md#host-tests) for standalone test commands and
[VALIDATION.md](VALIDATION.md) for current results and limitations. Host tests
exercise the production HTTP serializer/parser, network-card state, Wi-Fi file
loader, and browser program entry. Optional live host tests use a public echo
service with the computer's TLS transport; they do not test the badge radio.

With Web control enabled and BASIC ready, device checks are:

```sh
python3 tools/badge-http-get.py --host DEVICE_IP
python3 tools/badge-http-api.py --host DEVICE_IP
```

These replace the program in RAM. The GET tool only saves a disk file if given
`--save`, which overwrites `HTTP DEMO`; `--load` loads that file instead of typing
its source. Latest networking changes still need physical-device validation.
Public services can be slow or unavailable; inspect failures before retrying.
