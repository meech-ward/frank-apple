#!/usr/bin/env python3
"""Exercise the new HTTP API from BASIC on a real device with web control enabled.
Replaces only the program in RAM. Writes are sent only to httpbin's echo API.
"""
import argparse
import json
import re
from pathlib import Path
import time
import urllib.request
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host',required=True)
parser.add_argument('--logs',default='logs/http-api/live')
args=parser.parse_args();base='http://'+args.host
out=Path(args.logs);out.mkdir(parents=True,exist_ok=True)
def request(path,text=None):
    req=urllib.request.Request(base+path,data=text.encode('ascii') if text is not None else None,headers={'X-Apple2-Control':'1'} if text is not None else {})
    with urllib.request.urlopen(req,timeout=10) as reply:data=reply.read()
    return json.loads(data) if path=='/state' else data
def ready():
    deadline=time.monotonic()+50
    while time.monotonic()<deadline:
        try:
            state=request('/state')
            if state['basic_prompt']:return state
        except OSError:pass
        time.sleep(.3)
    raise RuntimeError('BASIC did not return to its prompt')
def line(s):request('/text',s+'\r');return ready()
ready();line('NEW')
source=(Path(__file__).resolve().parents[1]/'basic/http-api-demo.bas').read_text()
for s in source.splitlines():line(s)
# Scan response chunks for the unique echo marker rather than relying on what
# remains visible after a long JSON reply scrolls past the small display.
line('20 U$="https://httpbin.org/anything?marker=APPLECHECK"')
line('80 R$="":F=0')
line('90 GOSUB 9100:IF N=0 THEN PRINT "RESULT ";H;",";E;",";F:END')
line('100 R$=R$+B$:FOR J=1 TO LEN(R$)-9:IF MID$(R$,J,10)="APPLECHECK" THEN F=1')
line('110 NEXT J:R$=RIGHT$(R$,9):GOTO 90')
for method,name in enumerate(['GET','POST','PUT','PATCH','DELETE']):
    line('30 M='+str(method)+':H$="Content-Type: application/json"+CHR$(13)+"X-Apple-Test: APPLECHECK"')
    line('50 D$=""' if method==0 else '50 D$="{"+Q$+"message"+Q$+":"+Q$+"APPLECHECK"+Q$+"}"')
    state=line('RUN');(out/(name.lower()+'.json')).write_text(json.dumps(state,indent=2))
    assert re.search(r'RESULT\s+200,\s*0,\s*1',state['screen']),(name,state['screen'])
    print('PASS device '+name+': HTTP 200, complete response and echoed marker',flush=True)
