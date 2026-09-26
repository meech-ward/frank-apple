#!/usr/bin/env python3
"""Live public-API checks on the badge. Replaces the program in RAM.

Start at the Workshop BASIC prompt with Web control ON. --save explicitly saves
HTTP DEMO on the current DOS disk (overwrites that name), then reloads/runs it.
HTTP sends are never retried after an ambiguous failure; state reads may retry.
"""
import argparse
import json
from pathlib import Path
import re
import time
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True)
parser.add_argument('--logs', default='logs/http-get/live')
parser.add_argument('--save', action='store_true')
parser.add_argument('--load', action='store_true', help='Load the already-saved HTTP DEMO instead of typing it')
args = parser.parse_args()
directory = Path(args.logs)
directory.mkdir(parents=True, exist_ok=True)
base = 'http://' + args.host


def request(path, body=None):
    req = urllib.request.Request(base + path, data=body.encode('ascii') if body is not None else None,
                                 headers={'X-Apple2-Control': '1'} if body is not None else {})
    with urllib.request.urlopen(req, timeout=10) as reply:
        raw = reply.read()
    return json.loads(raw) if path == '/state' else raw


def ready(timeout=50):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            s = request('/state')
            if s['basic_prompt']:
                return s
        except OSError:
            pass
        time.sleep(.3)
    raise RuntimeError('BASIC did not return: ' + request('/state')['screen'])


def line(text):
    request('/text', text + '\r')
    return ready()


def capture(name, state):
    (directory / (name + '.json')).write_text(json.dumps(state, indent=2))


def run_case(name, url, status, error=0, contains=None):
    line('20 U$="' + url + '"')
    start = time.monotonic()
    state = line('RUN')
    capture(name + '-screen', state)
    if contains:
        assert contains in state['screen'], (name, state['screen'])
    result = line('PRINT "RESULT ";H;",";E')
    capture(name + '-result', result)
    assert re.search(r'RESULT\s+' + str(status) + ',' + str(error) + r'\s*\n', result['screen']), (name, result['screen'])
    print(f'PASS {name}: HTTP {status}, error {error}, {time.monotonic()-start:.1f}s', flush=True)


source = (Path(__file__).resolve().parents[1] / 'basic/http-demo.bas').read_text().splitlines()
ready()
if args.load:
    line('LOAD HTTP DEMO')
else:
    line('NEW')
    for index, text in enumerate(source, 1):
        line(text)
        if index % 10 == 0 or index == len(source):
            print(f'Entered {index}/{len(source)} BASIC lines', flush=True)
run_case('https-uuid', 'https://httpbin.org/uuid', 200, contains='uuid')
run_case('query', 'https://httpbin.org/get?message=HELLO_APPLE_II', 200, contains='HELLO_APPLE_II')
run_case('http-uuid', 'http://httpbin.org/uuid', 200, contains='uuid')
run_case('redirect', 'https://httpbin.org/relative-redirect/2', 200)
run_case('chunked', 'https://httpbin.org/stream/2', 200)
run_case('not-found', 'https://httpbin.org/status/404', 404)
run_case('no-content', 'https://httpbin.org/status/204', 204)
# Plain HTTP keeps this hop-limit test independent of four TLS handshakes
# consuming the separate 30-second request deadline on the badge.
run_case('redirect-limit', 'http://httpbin.org/redirect/5', 302, 8)
run_case('oversize', 'https://httpbin.org/bytes/4097', 200, 7)
run_case('bad-url', 'ftp://httpbin.org/uuid', 0, 2)
run_case('bad-certificate', 'https://self-signed.badssl.com/', 0, 10)
run_case('dns-error', 'http://badge-get-test.invalid/', 0, 3)
# /delay is capped at 10 seconds; /drip keeps a body incomplete past our deadline.
run_case('deadline', 'http://httpbin.org/drip?duration=40&numbytes=40&delay=0', 200, 5)

line('20 U$="https://httpbin.org/delay/10"')
request('/text', 'RUN\r')
time.sleep(4)
request('/key', 'break')
ready()
state = line('PRINT "CANCEL ";PEEK(49297);",";PEEK(49306)')
capture('cancel', state)
assert 'CANCEL 3,9' in state['screen'], state['screen']
print('PASS Ctrl-C cancels the active request', flush=True)

# Confirm that raw NUL/high-bit bytes survive on the real board.
line('70 S=0:FOR I=1 TO 5:S=S+PEEK(49303):NEXT I:PRINT "RAW ";S:END')
# httpbin's base64 endpoint decodes UTF-8: NUL, SOH, STX, C2 A0 survive exactly.
run_case('binary', 'https://httpbin.org/base64/AAECwqA=', 200, contains='RAW 357')
line(next(s for s in source if s.startswith('70 ')))

# The compatibility register now performs a public GET; cloud typing is absent.
line('POKE 49296,1')
time.sleep(7)
state = line('PRINT "LEGACY ";PEEK(49297);",";PEEK(49301)')
capture('legacy', state)
assert 'LEGACY 2,0' in state['screen'], state['screen']
print('PASS public compatibility GET; cloud keyboard disabled', flush=True)

run_case('final-uuid', 'https://httpbin.org/uuid', 200, contains='uuid')
if args.save:
    state = line('SAVE HTTP DEMO')
    capture('save', state)
    tail = state['screen'].split(']SAVE HTTP DEMO')[-1]
    assert not any(e in tail for e in ['DISK FULL', 'FILE LOCKED', 'WRITE PROTECTED', 'I/O ERROR']), tail
    time.sleep(3)
    line('NEW')
    line('LOAD HTTP DEMO')
    state = line('RUN')
    capture('saved-reloaded-run', state)
    assert 'uuid' in state['screen'] and 'HTTP 200' in state['screen'], state['screen']
    print('PASS HTTP DEMO saved, cleared from RAM, reloaded and run', flush=True)
