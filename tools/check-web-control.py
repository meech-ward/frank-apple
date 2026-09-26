#!/usr/bin/env python3
import urllib.request as u, urllib.error, json, time, socket
import argparse
parser=argparse.ArgumentParser(description='Exercise the real badge HTTP controller. Requires Web control ON and BASIC ready; replaces the program in RAM, but does not change saved files.')
parser.add_argument('--host',required=True,help='Badge IPv4 address shown in its menu')
args=parser.parse_args()
base='http://'+args.host
def req(path,body=None,header=True):
 r=u.Request(base+path,data=body,headers={'X-Apple2-Control':'1'} if header else {},method='POST' if body is not None else 'GET')
 try:
  with u.urlopen(r,timeout=12) as f:return f.status,f.read()
 except urllib.error.HTTPError as e:return e.code,e.read()
def state():
 status,b=req('/state');assert status==200;return json.loads(b)
def send(s):
 assert req('/type',s.encode())[0]==202
 until=time.monotonic()+25
 while state()['queued'] and time.monotonic()<until:time.sleep(.2)
 assert state()['queued']==0
 time.sleep(.8)
 return state()['screen']
def raw(s,split=None):
 with socket.create_connection((args.host,80),timeout=5) as sock:
  if split:
   sock.sendall(s[:split]);time.sleep(.2);sock.sendall(s[split:])
  else:sock.sendall(s)
  out=b''
  while True:
   b=sock.recv(8192)
   if not b:break
   out+=b
  return out
assert req('/')[0]==200
assert req('/type',b'PRINT 1\r',False)[0]==403
assert req('/type',b'x'*513)[0]==413
assert req('/type',b'\xff')[0]==400
assert req('/key',b'unknown')[0]==400
assert raw(b'POST /type HTTP/1.1\r\nHost: badge\r\nX-Apple2-Control: 1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx').startswith(b'HTTP/1.1 400')
assert raw(b'GET /state HTTP/1.1\r\nHost: badge\r\n\r\n',17).startswith(b'HTTP/1.1 200')
s=send('HOME\rPRINT 2+2\rPRINT "MULTILINE WORKS"\rPRINT 6*7\r')
print(s,flush=True)
assert 'MULTILINE WORKS' in s and '\n42 ' in s and '\n4 ' in s and 'SYNTAX ERROR' not in s
assert req('/key',b'menu')[0]==202;time.sleep(1)
assert state()['menu']
assert req('/type',b'PRINT 1\r')[0]==409
assert req('/key',b'menu')[0]==202;time.sleep(1)
assert not state()['menu']
s=send('NEW\r10 GOTO 10\rRUN\r')
assert req('/key',b'break')[0]==202;time.sleep(1)
s=state()['screen'];assert 'BREAK IN 10' in s,s
send('GR\rCOLOR=13:PLOT 20,20\r');assert state()['graphics']
s=send('TEXT:HOME\rPRINT "WEB CONTROL PASS"\r');assert not state()['graphics'] and 'WEB CONTROL PASS' in s
print('PASS: HTTP input, fragmented request, rejection limits, multiline pacing, menu, Stop, graphics state.',flush=True)
