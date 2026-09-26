#!/usr/bin/env python3
"""Production HTTP parser + disk importer, with lwIP transport stubs and real FatFs."""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
stubs=r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include "disk_library.h"
typedef int err_t;
typedef uint16_t u16_t;
struct tcp_pcb { int unused; };
struct pbuf { uint16_t tot_len; const void *payload; };
static struct tcp_pcb test_pcb;
#define ERR_OK 0
#define ERR_ABRT -1
#define TCP_WRITE_FLAG_COPY 1
#define IPADDR_TYPE_V4 0
#define IP_ANY_TYPE 0
#define SOF_REUSEADDR 0
#define MII_DEBUG_PRINTF(...) ((void)0)
#define tcp_arg(...) ((void)0)
#define tcp_recv(...) ((void)0)
#define tcp_sent(...) ((void)0)
#define tcp_err(...) ((void)0)
#define tcp_poll(...) ((void)0)
#define tcp_accept(...) ((void)0)
#define tcp_nagle_disable(...) ((void)0)
#define tcp_recved(...) ((void)0)
#define tcp_close(...) 0
#define tcp_abort(...) ((void)0)
#define tcp_sndbuf(...) 65535
#define tcp_write(...) 0
#define tcp_output(...) ((void)0)
#define tcp_new_ip_type(...) (&test_pcb)
#define tcp_bind(...) 0
#define tcp_listen_with_backlog(...) (&test_pcb)
#define ip_set_option(...) ((void)0)
#define pbuf_free(...) ((void)0)
#define pbuf_copy_partial(p,d,n,o) memcpy(d,(const char *)(p)->payload+(o),n)
static int *netif_default;
#define netif_ip4_addr(...) 0
#define ip4_addr_isany_val(...) 1
#define ip4addr_ntoa(...) "0.0.0.0"
static const unsigned char web_page[]="<html></html>";
size_t typing_pending(void){return 0;}
bool typing_try_literal(const uint8_t *p,size_t n){return true;}
bool typing_try_push(const uint8_t *p,size_t n){return true;}
bool typing_try_apple(uint8_t c){return true;}
bool disk_ui_is_visible(void){return false;}
bool remote_control_graphics(void){return false;}
bool remote_control_basic_prompt(void){return true;}
unsigned remote_control_columns(void){return 40;}
size_t remote_control_screen(char *text,char *inverse,size_t cap){text[0]=inverse[0]=0;return 0;}
void remote_control_key(uint8_t key){}
const char *netcard_wifi_status(void){return NULL;}
static unsigned mounts;
bool disk_ui_mount_file(const char *name,int drive,bool boot){assert(!strcmp(name,"Test.dsk")&&drive==0&&boot);mounts++;return true;}
'''
fixture=r'''
static web_client_t client;
static unsigned char request_bytes[2400];
static int exchange(const char *method,const char *path,const void *body,unsigned n,bool control,unsigned split){
 memset(&client,0,sizeof(client));client.pcb=&test_pcb;
 int head=snprintf((char*)request_bytes,sizeof(request_bytes),"%s %s HTTP/1.1\r\nHost: apple\r\n%sContent-Length: %u\r\n\r\n",method,path,control?"X-Apple2-Control: 1\r\n":"",n);
 if(n)memcpy(request_bytes+head,body,n);unsigned total=head+n;
 for(unsigned i=0;i<total;){unsigned take=total-i;if(take>split)take=split;struct pbuf p={.tot_len=take,.payload=request_bytes+i};assert(received(&client,&test_pcb,&p,0)==0);i+=take;}
 int status=0;assert(client.responding);assert(sscanf(client.header,"HTTP/1.1 %d",&status)==1);return status;
}
int main(void){
 FATFS fs;BYTE work[4096];MKFS_PARM params={.fmt=FM_FAT|FM_SFD,.au_size=2048};
 assert(f_mkfs("",&params,work,sizeof(work))==FR_OK&&f_mount(&fs,"",1)==FR_OK);
 const char *start="143360\nTest.dsk";
 assert(exchange("POST","/upload/start",start,strlen(start),false,300)==403);
 assert(exchange("POST","/upload/start",start,strlen(start),true,7)==201);
 unsigned id;assert(sscanf(client.body,"{\"id\":%u",&id)==1);
 unsigned char packet[1033]={0};put32(packet,id);
 assert(exchange("POST","/upload/chunk",packet,1033,true,1500)==413);
 assert(exchange("POST","/type",packet,513,true,1500)==413);
 for(unsigned i=0;i<143360;i+=1024){put32(packet+4,i);memset(packet+8,i/1024,1024);assert(exchange("POST","/upload/chunk",packet,1032,true,137)==200);}
 char number[32];snprintf(number,sizeof(number),"%u",id);
 assert(exchange("POST","/upload/finish",number,strlen(number),true,1)==201);
 assert(exchange("GET","/disks",NULL,0,false,1500)==200&&strstr(client.body,"Test.dsk"));
 assert(exchange("GET","/disks?offset=24",NULL,0,false,1500)==200);
 assert(exchange("GET","/disks?offset=-1",NULL,0,false,1500)==400);
 const char *mount="boot\n0\nTest.dsk";
 assert(exchange("POST","/disks/mount",mount,strlen(mount),true,1500)==202);assert(mounts==0);
 assert(exchange("GET","/mount",NULL,0,false,1500)==200&&strstr(client.body,"\"pending\":true"));
 assert(exchange("POST","/type","x",1,true,1500)==409);
 now+=250001;web_control_poll();assert(mounts==1);
 assert(exchange("GET","/mount",NULL,0,false,1500)==200&&strstr(client.body,"\"ok\":true"));
 puts("PASS: HTTP fragmented binary upload, opt-in header, endpoint-specific size limits, disk list pagination and deferred mount.");
}
'''
with tempfile.TemporaryDirectory(prefix='apple2-web-') as directory:
 p=Path(directory);(p/'pico').mkdir();(p/'pico/time.h').write_text('#include <stdint.h>\nuint64_t time_us_64(void);\n')
 source=re.sub(r'^#include[^\n]*\n','', (root/'src/web_control.c').read_text(),flags=re.M)
 (p/'web-under-test.c').write_text(source)
 (p/'check.c').write_text('#define main storage_tests_main\n#include "'+str(root/'tools/test-disk-library.c')+'"\n#undef main\n'+stubs+'\n#include "web-under-test.c"\n'+fixture)
 fat=root/'drivers/fatfs'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-value','-Wno-unused-but-set-variable','-g','-fsanitize=address,undefined','-I',str(p),'-I',str(root/'src'),'-I',str(fat),str(p/'check.c'),str(root/'src/disk_library.c'),*[str(fat/n) for n in ['ff.c','ffsystem.c','ffunicode.c']],'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
