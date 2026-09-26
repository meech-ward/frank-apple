#!/usr/bin/env python3
"""Check production INI parsing and FatFs loading with bounded, fake file reads."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
fixture = r'''
#include "wifi_config.h"
#include "ff.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static const char *file_text;
static size_t file_size;
static unsigned closes;
static int open_result=FR_OK,read_result=FR_OK,close_result=FR_OK;
FRESULT f_open(FIL *fp,const char *path,int mode){assert(!strcmp(path,"/wifi.ini")&&mode==FA_READ);return open_result;}
FRESULT f_read(FIL *fp,void *out,UINT size,UINT *count){*count=file_size<size?file_size:size;memcpy(out,file_text,*count);return read_result;}
FRESULT f_close(FIL *fp){closes++;return close_result;}
static void bad(const char *s,size_t n){wifi_config c;memset(&c,0xab,sizeof(c));assert(!wifi_config_parse(s,n,&c));for(size_t i=0;i<sizeof(c);i++)assert(!((char *)&c)[i]);}
int main(void){
 wifi_config c;
 const char *valid="[wifi]\nssid=My WiFi\npassword=Password123\n";
 assert(wifi_config_parse(valid,strlen(valid),&c));assert(!strcmp(c.ssid,"My WiFi")&&!strcmp(c.password,"Password123"));
 const char *quoted="\xef\xbb\xbf# File\r\n[wifi]\r\nssid = ' Cafe ' \r\npassword = \" #a;=b\\c \"\r\n";
 assert(wifi_config_parse(quoted,strlen(quoted),&c));assert(!strcmp(c.ssid," Cafe ")&&!strcmp(c.password," #a;=b\\c "));
 const char *open="[wifi]\nssid=Open network\npassword=";
 assert(wifi_config_parse(open,strlen(open),&c)&&!c.password[0]);
 const char *invalid[]={"","[wifi]\nssid=Only name","[wifi]\nssid=\npassword=Password123","ssid=No section\npassword=Password123",
 "[wifi]\nssid=Name\npassword=short","[wifi]\nssid=Name\npassword=Password123\nssid=Other","[wifi]\nssid=Name\npassword=Password123\ntypo=yes",
 "[wifi]\nssid=Name\npassword=\"unclosed","[wifi]\nssid=Name\npassword=Password123\n[wifi]","[wifi]\nssid=Name\npassword=Password123\n[other]"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)bad(invalid[i],strlen(invalid[i]));
 char buf[600];memset(buf,'x',sizeof(buf));bad(buf,sizeof(buf));bad(buf,128);
 char max[200];snprintf(max,sizeof(max),"[wifi]\nssid=%032d\npassword=%064d",0,0);assert(wifi_config_parse(max,strlen(max),&c));
 max[strlen(max)-1]='Z';bad(max,strlen(max));
 memcpy(buf,valid,strlen(valid));buf[10]=0;bad(buf,strlen(valid));
 file_text=valid;file_size=strlen(valid);
 assert(wifi_config_load(&c)==WIFI_CONFIG_READY&&closes==1);
 for(int e=FR_NO_FILE;e<=FR_NOT_ENABLED;e++){open_result=e;assert(wifi_config_load(&c)==WIFI_CONFIG_MISSING&&!c.ssid[0]);}
 open_result=99;assert(wifi_config_load(&c)==WIFI_CONFIG_IO);
 open_result=FR_OK;read_result=99;assert(wifi_config_load(&c)==WIFI_CONFIG_IO&&!c.ssid[0]);read_result=FR_OK;
 close_result=99;assert(wifi_config_load(&c)==WIFI_CONFIG_IO);close_result=FR_OK;
 file_text=buf;file_size=sizeof(buf);assert(wifi_config_load(&c)==WIFI_CONFIG_INVALID);
 unsigned seed=42;for(int n=0;n<1000;n++){for(size_t i=0;i<sizeof(buf);i++){seed=seed*1664525u+1013904223u;buf[i]=seed>>24;}wifi_config_parse(buf,n%sizeof(buf),&c);}
 puts("PASS: Wi-Fi config, literal punctuation/quoted spaces, CRLF/BOM, open networks, key lengths, malformed files, FatFs errors and bounds.");
}
'''
with tempfile.TemporaryDirectory(prefix='apple2-wifi-') as directory:
 p=Path(directory)
 (p/'ff.h').write_text('typedef int FRESULT; typedef unsigned UINT; typedef struct { int x; } FIL;\nenum { FR_OK,FR_NO_FILE,FR_NO_PATH,FR_NOT_READY,FR_NOT_ENABLED };\n#define FA_READ 1\nFRESULT f_open(FIL *,const char *,int);FRESULT f_read(FIL *,void *,UINT,UINT *);FRESULT f_close(FIL *);\n')
 (p/'check.c').write_text(fixture)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g','-fsanitize=address,undefined','-I',str(p),'-I',str(root/'src'),str(p/'check.c'),str(root/'src/wifi_config.c'),str(root/'src/wifi_config_file.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
