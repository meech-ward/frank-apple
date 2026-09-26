#!/usr/bin/env python3
"""Live echo tests using production HTTP serialization/parsing on the host.

Uses the host's TCP/TLS transport, not the badge's radio/lwIP. Hardware tests
remain separate. Writes go only to httpbin's non-persistent echo endpoint.
"""
import ctypes
import json
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import time
from urllib.parse import urlsplit
root=Path(__file__).resolve().parents[1]
wrapper=r'''
#include "net_http.h"
static nh_response response;
int prepare(char *out,const char *url,unsigned method,const char *headers,const unsigned char *body,unsigned size){
 nh_url parsed;int e=nh_parse_url(url,&parsed);if(e)return -e;
 nh_init(&response);response.redirect_headers_only=false;
 return nh_request(out,NH_REQUEST_MAX,&parsed,method,headers,body,size);
}
void receive(const unsigned char *bytes,unsigned size){nh_feed(&response,bytes,size);}
void finish(void){nh_eof(&response);}
unsigned status(void){return response.status;}
unsigned error(void){return response.error;}
unsigned length(void){return (unsigned)response.size;}
const unsigned char *body(void){return response.body;}
'''
with tempfile.TemporaryDirectory(prefix='apple2-http-live-') as tmp:
 p=Path(tmp);(p/'wrapper.c').write_text(wrapper)
 subprocess.run(['cc','-std=c11','-shared','-fPIC','-I',str(root/'src'),str(p/'wrapper.c'),str(root/'src/net_http.c'),'-o',str(p/'http.dylib')],check=True)
 lib=ctypes.CDLL(str(p/'http.dylib'))
 lib.prepare.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_uint,ctypes.c_char_p,ctypes.c_void_p,ctypes.c_uint]
 lib.receive.argtypes=[ctypes.c_void_p,ctypes.c_uint]
 lib.body.restype=ctypes.c_void_p
 for index,verb in enumerate(['GET','POST','PUT','PATCH','DELETE']):
  url='https://httpbin.org/anything?message=HelloApple'
  data=b'' if not index else b'{"message":"HELLO FROM APPLE II","score":42}'
  headers=b'Content-Type: application/json\rapikey: PUBLIC_TEST_VALUE\rPrefer: return=representation'
  out=ctypes.create_string_buffer(4096)
  size=lib.prepare(out,url.encode(),index,headers,data,len(data));assert size>0
  parsed=urlsplit(url);start=time.monotonic()
  with socket.create_connection((parsed.hostname,443),timeout=15) as connection:
   with ssl.create_default_context().wrap_socket(connection,server_hostname=parsed.hostname) as tls:
    tls.sendall(out.raw[:size])
    while chunk:=tls.recv(1024):
     lib.receive(chunk,len(chunk))
     if lib.error():break
  lib.finish()
  assert lib.error()==0 and lib.status()==200,(verb,lib.error(),lib.status())
  reply=json.loads(ctypes.string_at(lib.body(),lib.length()))
  assert reply['method']==verb and reply['args']['message']=='HelloApple'
  echoed={k.lower():v for k,v in reply['headers'].items()}
  assert echoed['apikey']=='PUBLIC_TEST_VALUE' and echoed['prefer']=='return=representation'
  if data:assert reply['json']==json.loads(data)
  print(f'PASS host HTTPS {verb}: exact query, explicit headers'+(', JSON body' if data else '')+f' ({time.monotonic()-start:.1f}s)',flush=True)
