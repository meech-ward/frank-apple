#!/usr/bin/env python3
"""Exercise real hotspot DHCP parsing/leases with address/UB sanitizers."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
fixture = r'''
#include "wifi_dhcp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t request[640], reply[WIFI_DHCP_PACKET_MAX], dest[4];
static size_t length;
static wifi_dhcp_server server;
static const uint8_t ip[4]={192,168,4,1}, first[4]={192,168,4,2};
static void add(uint8_t tag,const uint8_t *value,size_t n){assert(length+n+3<sizeof(request));request[length++]=tag;request[length++]=(uint8_t)n;memcpy(request+length,value,n);length+=n;request[length]=255;}
static void begin(uint8_t type,uint8_t client){
 memset(request,0,sizeof(request));request[0]=1;request[1]=1;request[2]=6;
 request[4]=0x12;request[5]=0x34;request[6]=0xab;request[7]=0xcd;
 request[28]=2;request[33]=client;
 request[236]=99;request[237]=130;request[238]=83;request[239]=99;
 length=240;add(53,&type,1);
}
static size_t send_at(uint32_t now){return wifi_dhcp_reply(&server,request,length+1,now,reply,sizeof(reply),dest);}
static const uint8_t *get(uint8_t tag,size_t n){
 size_t at=240;
 while(at<sizeof(reply)){
  uint8_t code=reply[at++];if(code==255)return NULL;if(!code)continue;
  assert(at<sizeof(reply));size_t size=reply[at++];assert(at+size<=sizeof(reply));
  if(code==tag){assert(size==n);return reply+at;}at+=size;
 }
 assert(0);return NULL;
}
static void expect(uint8_t type,uint8_t host){
 assert(reply[0]==2&&reply[1]==1&&reply[2]==6);
 assert(!memcmp(reply+4,request+4,4)&&!memcmp(reply+28,request+28,6));
 assert(get(53,1)&&*get(53,1)==type);assert(get(54,4)&&!memcmp(get(54,4),ip,4));
 assert(reply[19]==host);assert(!get(3,4)&&!get(6,4));
}
static void discover(uint8_t client,uint32_t now,uint8_t host){begin(1,client);assert(send_at(now)>=300);expect(2,host);assert(dest[0]==255&&dest[3]==255);}
static void bind(uint8_t client,uint8_t host,uint32_t now){uint8_t wanted[4]={192,168,4,host};begin(3,client);add(50,wanted,4);add(54,ip,4);assert(send_at(now));expect(5,host);}
static void ignored(void){wifi_dhcp_server before=server;assert(!send_at(10));assert(!memcmp(&before,&server,sizeof(server)));}

int main(void){
 wifi_dhcp_init(&server);
 discover(1,1,2);
 assert(!memcmp(get(1,4),(uint8_t[]){255,255,255,0},4));
 assert(!memcmp(get(51,4),(uint8_t[]){0,0,14,16},4));
 discover(1,2,2); /* Repeat DISCOVER does not consume a second lease. */
 bind(1,2,3);
 begin(3,1);memcpy(request+12,first,4);assert(send_at(1800));expect(5,2);
 assert(!memcmp(dest,first,4)&&!memcmp(reply+12,first,4));
 begin(3,1);add(50,first,4);assert(send_at(1801));expect(5,2); /* INIT-REBOOT known client. */
 begin(7,2);memcpy(request+12,first,4);add(54,ip,4);assert(!send_at(1802));
 discover(2,1803,3); /* Another MAC cannot release an existing lease. */
 begin(7,1);memcpy(request+12,first,4);add(54,ip,4);assert(!send_at(1804));
 discover(3,1805,2);

 wifi_dhcp_init(&server);
 for(uint8_t i=1;i<=WIFI_DHCP_LEASES;i++){discover(i,1,(uint8_t)(i+1));bind(i,(uint8_t)(i+1),2);}
 begin(1,9);assert(!send_at(10)); /* Full pool must not evict a live session. */
 discover(1,100,2);
 begin(1,9);assert(!send_at(200)); /* Rediscovery didn't shorten its bound lease. */
 discover(9,3603,2);
 discover(8,3664,2); /* Unaccepted offer expires in sixty seconds. */

 wifi_dhcp_init(&server);
 discover(1,1,2);bind(1,2,2);
 begin(3,2);add(50,first,4);add(54,ip,4);assert(send_at(3));expect(6,0);assert(dest[0]==255);
 begin(3,2);add(50,first,4);assert(!send_at(4)); /* Unknown cached local address: no conflicting ACK. */
 begin(3,2);add(50,(uint8_t[]){10,0,0,8},4);assert(send_at(4));expect(6,0);
 begin(3,1);memcpy(request+12,first,4);add(50,first,4);assert(!send_at(5)); /* Inconsistent client states. */
 begin(3,1);add(54,ip,4);assert(!send_at(5));
 begin(4,1);add(50,first,4);add(54,ip,4);assert(!send_at(5));
 discover(2,6,3); /* DECLINE quarantines the address. */
 discover(3,606,2);

 wifi_dhcp_init(&server);
 begin(8,1);memcpy(request+12,(uint8_t[]){192,168,4,80},4);assert(send_at(1));expect(5,0);
 assert(!get(51,4)&&!get(58,4)&&!get(59,4)&&dest[3]==80);
 discover(1,2,2); /* INFORM didn't allocate a lease. */

 wifi_dhcp_init(&server);
 uint8_t identity[255];for(unsigned i=0;i<sizeof(identity);i++)identity[i]=(uint8_t)i;
 begin(1,1);add(61,identity,sizeof(identity));assert(send_at(1)>500);expect(2,2);
 assert(!memcmp(get(61,255),identity,sizeof(identity)));
 begin(3,1);add(50,first,4);add(54,ip,4);add(61,identity,sizeof(identity));assert(send_at(2));expect(5,2);
 begin(3,2);memcpy(request+12,first,4);add(61,identity,sizeof(identity));assert(send_at(3));expect(5,2); /* ID, not MAC, identifies this lease. */

 /* Validate every fixed-field boundary and important option shape. */
 wifi_dhcp_init(&server);
 begin(1,1);request[0]=2;ignored();
 begin(1,1);request[1]=2;ignored();
 begin(1,1);request[2]=7;ignored();
 begin(1,1);request[3]=1;ignored();
 begin(1,1);request[24]=10;ignored();
 begin(1,1);request[28]=1;ignored();
 begin(1,1);memset(request+28,0,6);ignored();
 begin(1,1);request[236]=0;ignored();
 begin(1,1);add(54,(uint8_t[]){192,168,4,99},4);ignored();
 begin(1,1);add(53,(uint8_t[]){1},1);ignored();
 begin(1,1);add(50,first,3);ignored();
 begin(1,1);add(54,ip,3);ignored();
 begin(1,1);add(61,identity,1);ignored();
 begin(1,1);add(61,identity,7);add(61,identity,7);ignored();
 begin(1,1);add(50,first,4);add(50,first,4);ignored();
 begin(1,1);add(54,ip,4);add(54,ip,4);ignored();
 begin(1,1);add(52,(uint8_t[]){1},1);ignored();
 begin(1,1);request[length]=12;ignored(); /* Truncated option length. */
 begin(1,1);request[241]=8;ignored(); /* Truncated option body. */
 begin(1,1);request[240]=0;request[241]=0;request[242]=0;ignored(); /* Missing message type. */
 begin(1,1);request[length]=0;ignored(); /* Missing END. */
 begin(1,1);for(size_t n=0;n<length+1;n++)assert(!wifi_dhcp_reply(&server,request,n,1,reply,sizeof(reply),dest));
 assert(!wifi_dhcp_reply(&server,request,sizeof(request),1,reply,sizeof(reply),dest));
 assert(!wifi_dhcp_reply(&server,request,length+1,1,reply,sizeof(reply)-1,dest));
 assert(!wifi_dhcp_reply(NULL,request,length+1,1,reply,sizeof(reply),dest));

 /* Realistic option padding/unknown options and requested free addresses. */
 begin(1,1);add(50,(uint8_t[]){192,168,4,5},4);add(12,(uint8_t *)"macbook",7);add(55,(uint8_t[]){1,3,6,15,119,252},6);
 request[length++]=0;request[length++]=0;request[length]=255;assert(send_at(1));expect(2,5);

 /* Monotonic uint32_t wrap is safe for one-hour leases. */
 wifi_dhcp_init(&server);discover(1,UINT32_MAX-10,2);bind(1,2,UINT32_MAX-9);
 begin(3,1);memcpy(request+12,first,4);assert(send_at(100));expect(5,2);

 /* Deterministic parser fuzz, including mutations of a plausible header. */
 unsigned seed=42;uint8_t valid[sizeof(request)];begin(1,1);memcpy(valid,request,sizeof(valid));
 for(unsigned n=0;n<20000;n++){
  for(size_t i=0;i<sizeof(request);i++){seed=seed*1664525u+1013904223u;request[i]=(uint8_t)(seed>>24);}
  if(n&1){memcpy(request,valid,240);seed=seed*1664525u+1013904223u;request[seed%240]^=(uint8_t)(seed>>24);}
  size_t count=wifi_dhcp_reply(&server,request,n%sizeof(request),n,reply,sizeof(reply),dest);
  assert(!count||(count>=300&&count<=sizeof(reply)));
 }
 puts("PASS: DHCP offer/ACK, renewal, client identifiers, release/decline, pool/expiry/wrap, INFORM, malformed/oversize packets, 20,000 parser fuzz cases.");
}
'''
with tempfile.TemporaryDirectory(prefix="apple2-dhcp-") as directory:
    p = Path(directory)
    (p / "check.c").write_text(fixture)
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-I", str(root / "src"),
        str(p / "check.c"), str(root / "src/wifi_dhcp.c"), "-o", str(p / "check"),
    ], check=True)
    subprocess.run([str(p / "check")], check=True)
