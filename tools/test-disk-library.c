// Exercise the real importer against the real FatFs, on a memory-backed volume.
#include "disk_library.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SECTORS 24576u
static BYTE media[SECTORS*512u];
static uint64_t now;
static bool fail_write,fail_sync;
uint64_t time_us_64(void){return now;}
DSTATUS disk_initialize(BYTE d){return d?STA_NOINIT:0;}
DSTATUS disk_status(BYTE d){return d?STA_NOINIT:0;}
DRESULT disk_read(BYTE d,BYTE *p,LBA_t sector,UINT n){if(d||sector>=SECTORS||n>SECTORS-sector)return RES_PARERR;memcpy(p,media+sector*512u,n*512u);return RES_OK;}
DRESULT disk_write(BYTE d,const BYTE *p,LBA_t sector,UINT n){if(fail_write)return RES_ERROR;if(d||sector>=SECTORS||n>SECTORS-sector)return RES_PARERR;memcpy(media+sector*512u,p,n*512u);return RES_OK;}
DRESULT disk_ioctl(BYTE d,BYTE cmd,void *p){if(d)return RES_PARERR;switch(cmd){case CTRL_SYNC:return fail_sync?RES_ERROR:RES_OK;case GET_SECTOR_COUNT:*(LBA_t*)p=SECTORS;break;case GET_SECTOR_SIZE:*(WORD*)p=512;break;case GET_BLOCK_SIZE:*(DWORD*)p=8;break;default:return RES_PARERR;}return RES_OK;}
DWORD get_fattime(void){return 0;}
static char reply[6400];
static uint32_t begin(const char *name,unsigned size){unsigned id;int code=library_begin(name,size,reply,sizeof(reply));if(code!=201)fprintf(stderr,"begin %s: %d %s\n",name,code,reply);assert(code==201);assert(sscanf(reply,"{\"id\":%u",&id)==1);return id;}
static void chunks(uint32_t id,const BYTE *data,unsigned size,unsigned step){for(unsigned i=0;i<size;){unsigned n=size-i;if(n>step)n=step;assert(library_chunk(id,i,data+i,n,reply,sizeof(reply))==200);i+=n;}}
static void exists(const char *name,bool expected){FILINFO f;char path[100];snprintf(path,sizeof(path),"/apple/%s",name);assert((f_stat(path,&f)==FR_OK)==expected);}
static void same(const char *name,const BYTE *bytes,unsigned n){FIL f;UINT got;BYTE buf[1024];char path[100];snprintf(path,sizeof(path),"/apple/%s",name);assert(f_open(&f,path,FA_READ)==FR_OK);assert(f_size(&f)==n);for(unsigned i=0;i<n;){unsigned len=n-i;if(len>sizeof(buf))len=sizeof(buf);assert(f_read(&f,buf,len,&got)==FR_OK&&got==len&&!memcmp(buf,bytes+i,len));i+=len;}assert(f_close(&f)==FR_OK);}
static void put32(BYTE *p,unsigned n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
int main(void){
 FATFS fs;BYTE work[4096];MKFS_PARM params={.fmt=FM_FAT|FM_SFD,.n_fat=2,.au_size=2048};
 assert(f_mkfs("",&params,work,sizeof(work))==FR_OK);assert(f_mount(&fs,"",1)==FR_OK);
 static BYTE data[143360];for(unsigned i=0;i<sizeof(data);i++)data[i]=(i*17u)^(i>>8);
 assert(library_begin("../wifi.ini",143360,reply,sizeof(reply))==400);
 assert(library_begin("app.zip",143360,reply,sizeof(reply))==400);
 assert(library_begin("x.po",819200,reply,sizeof(reply))==400);
 assert(library_begin(".x.dsk",143360,reply,sizeof(reply))==400);
 assert(library_begin("foo\".dsk",143360,reply,sizeof(reply))==400);
 uint32_t id=begin("My App.dsk",sizeof(data));exists("My App.dsk",false);
 assert(library_begin("Other.dsk",sizeof(data),reply,sizeof(reply))==409);
 assert(library_chunk(id+1,0,data,512,reply,sizeof(reply))==409);
 assert(library_chunk(id,1,data,512,reply,sizeof(reply))==409);
 assert(library_finish(id,reply,sizeof(reply))==409);
 chunks(id,data,sizeof(data),997); // deliberately crosses the 4 KB buffer boundary.
 assert(library_chunk(id,sizeof(data),data,1,reply,sizeof(reply))==409);
 assert(library_finish(id,reply,sizeof(reply))==201);same("My App.dsk",data,sizeof(data));
 assert(library_begin("my app.DSK",sizeof(data),reply,sizeof(reply))==409);
 assert(library_list(0,reply,sizeof(reply))==200&&strstr(reply,"My App.dsk")&&!strstr(reply,".upload.part"));
 id=begin("Cancelled.po",sizeof(data));chunks(id,data,3072,1024);assert(library_cancel(id+1,reply,sizeof(reply))==409);assert(library_cancel(id,reply,sizeof(reply))==200);exists("Cancelled.po",false);exists(".upload.part",false);
 id=begin("Timed Out.dsk",sizeof(data));now+=60000001;library_poll();assert(!library_uploading());exists(".upload.part",false);assert(library_finish(id,reply,sizeof(reply))==409);
 id=begin("Broken.dsk",sizeof(data));fail_write=true;int code=200;for(unsigned i=0;i<4096&&code==200;i+=1024)code=library_chunk(id,i,data+i,1024,reply,sizeof(reply));assert(code==507);fail_write=false;library_shutdown();assert(!library_uploading());exists("Broken.dsk",false);same("My App.dsk",data,sizeof(data));
 id=begin("Sync Error.dsk",sizeof(data));chunks(id,data,sizeof(data),1024);fail_sync=true;assert(library_finish(id,reply,sizeof(reply))==507);fail_sync=false;library_shutdown();assert(!library_uploading());exists("Sync Error.dsk",false);
 // Malformed WOZ chunk lengths and wrong disk type never enter the library.
 BYTE woz[2048]={0};memcpy(woz,"WOZ2\xff\x0a\x0d\x0a",8);memcpy(woz+12,"INFO",4);put32(woz+16,0xfffffff0);
 id=begin("Bad.woz",sizeof(woz));chunks(id,woz,sizeof(woz),1024);assert(library_finish(id,reply,sizeof(reply))==400);exists("Bad.woz",false);
 // Minimal structurally valid one-track WOZ2, with bounds-checked track table.
 memset(woz,0,sizeof(woz));memcpy(woz,"WOZ2\xff\x0a\x0d\x0a",8);memcpy(woz+12,"INFO",4);put32(woz+16,60);woz[20]=2;woz[21]=1;
 memcpy(woz+80,"TMAP",4);put32(woz+84,160);memset(woz+88,255,160);woz[88]=0;
 memcpy(woz+248,"TRKS",4);put32(woz+252,1792);woz[256]=3;woz[258]=1;put32(woz+260,4096);
 id=begin("Track.woz",sizeof(woz));chunks(id,woz,sizeof(woz),1000);assert(library_finish(id,reply,sizeof(reply))==201);same("Track.woz",woz,sizeof(woz));
 woz[88]=35;id=begin("Bad Track.woz",sizeof(woz));chunks(id,woz,sizeof(woz),1024);assert(library_finish(id,reply,sizeof(reply))==400);
 // A working copy cannot be shadowed by uploading its original under the old name.
 FIL f;assert(f_open(&f,"/apple/Saved.dsk.bdsk",FA_CREATE_NEW|FA_WRITE)==FR_OK);assert(f_close(&f)==FR_OK);
 assert(library_begin("Saved.dsk",sizeof(data),reply,sizeof(reply))==409);
 // Pagination is bounded even when the SD has many disks; no Apple RAM disk list used.
 for(unsigned i=0;i<30;i++){char name[70];snprintf(name,sizeof(name),"/apple/List %02u.dsk",i);assert(f_open(&f,name,FA_CREATE_NEW|FA_WRITE)==FR_OK);assert(f_close(&f)==FR_OK);}
 assert(library_list(0,reply,sizeof(reply))==200&&strstr(reply,"\"next\":24"));assert(library_list(24,reply,sizeof(reply))==200&&strstr(reply,"List 29.dsk")&&strstr(reply,"\"next\":0"));
 // Exhaust free clusters and ensure begin refuses before making a staging file.
 assert(f_open(&f,"/full",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);UINT wrote;do{assert(f_write(&f,work,sizeof(work),&wrote)==FR_OK);}while(wrote==sizeof(work));assert(f_close(&f)==FR_OK);
 assert(library_begin("No Room.dsk",sizeof(data),reply,sizeof(reply))==507);exists("No Room.dsk",false);same("My App.dsk",data,sizeof(data));
 puts("PASS: real FatFs upload/readback, arbitrary chunk boundaries, duplicates/saves, cancellation/expiry, malformed WOZ, storage failures, pagination and full volume.");
 return 0;
}
