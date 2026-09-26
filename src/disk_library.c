// Original downstream code. MIT; see LICENSE-DOWNSTREAM.
// All calls run on core 0 between emulator iterations. Use a separate FatFs FIL;
// never reuse the emulator's fp or its disk list (which occupies Apple RAM).
#include "disk_library.h"
#include "ff.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>

#define PART "/apple/.upload.part"
#define DSK_SIZE 143360u
#define NIB_SIZE 232960u
#define BDSK_SIZE (8u + 35u * 6660u)
#define RESERVE (512u * 1024u) // Working image, menu snapshot, FAT/directory space.
static struct {
    FIL file;
    bool active, discarding;
    uint32_t id, size, received;
    uint64_t touched;
    char name[LIBRARY_NAME_MAX + 1];
    uint8_t buffer[4096];
    size_t buffered;
} upload;
static uint32_t next_id;
static int reply_error(char *out, size_t cap, int status, const char *text) {
    snprintf(out, cap, "%s", text); return status;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1]<<8; }
static const char *extension(const char *name) { const char *p=strrchr(name,'.'); return p?p:""; }
static bool supported(const char *name) {
    const char *e=extension(name);
    return !strcasecmp(e,".dsk") || !strcasecmp(e,".do") || !strcasecmp(e,".po") ||
           !strcasecmp(e,".nib") || !strcasecmp(e,".woz") || !strcasecmp(e,".bdsk");
}
bool library_name_valid(const char *name) {
    size_t n=strlen(name);
    if(!n || n>LIBRARY_NAME_MAX || name[0]=='.' || name[0]==' ' || name[n-1]==' ' || name[n-1]=='.') return false;
    for(size_t i=0;i<n;i++) if((unsigned char)name[i]<32 || (unsigned char)name[i]>126 || strchr("/\\:*?\"<>|",name[i])) return false;
    return supported(name);
}
static bool size_valid(const char *name, uint32_t n) {
    const char *e=extension(name);
    if(!strcasecmp(e,".dsk") || !strcasecmp(e,".do") || !strcasecmp(e,".po")) return n==DSK_SIZE;
    if(!strcasecmp(e,".nib")) return n==NIB_SIZE;
    if(!strcasecmp(e,".bdsk")) return n==BDSK_SIZE;
    return !strcasecmp(e,".woz") && n>=256 && n<=LIBRARY_MAX_FILE;
}
static bool free_space(uint64_t *bytes) {
    DWORD clusters; FATFS *fs;
    if(f_getfree("",&clusters,&fs)!=FR_OK) return false;
    *bytes=(uint64_t)clusters*fs->csize*512u; return true;
}
static bool target_available(const char *name) {
    char path[96]; FILINFO info;
    snprintf(path,sizeof(path),"/apple/%s",name);
    if(f_stat(path,&info)!=FR_NO_FILE) return false;
    snprintf(path,sizeof(path),"/apple/%s.bdsk",name);
    return f_stat(path,&info)==FR_NO_FILE;
}
bool library_uploading(void) { return upload.active; }
void library_shutdown(void) {
    upload.buffered=0;
    if(upload.active) {
        upload.discarding=true; upload.touched=time_us_64();
        // FatFs keeps a failed-close FIL locked. Retain it and retry cleanup;
        // reusing that object would permanently lose its lock/cluster state.
        if(f_close(&upload.file)!=FR_OK) return;
        upload.active=false; upload.discarding=false; f_unlink(PART);
    }
}
void library_poll(void) {
    if(upload.active && time_us_64()-upload.touched>(upload.discarding?1000000u:60000000u)) library_shutdown();
}
static int progress(char *out,size_t cap) {
    snprintf(out,cap,"{\"id\":%lu,\"received\":%lu,\"size\":%lu}",
             (unsigned long)upload.id,(unsigned long)upload.received,(unsigned long)upload.size);
    return 200;
}
int library_begin(const char *name,uint32_t size,char *out,size_t cap) {
    library_poll();
    if(upload.active) return reply_error(out,cap,409,"Another upload is in progress. Finish or cancel it, or wait one minute.");
    if(!library_name_valid(name)) return reply_error(out,cap,400,"Choose a disk image with a simple filename (up to 58 characters). Supported: .dsk, .do, .po, .nib, .woz, .bdsk. Unzip archives first.");
    if(!size_valid(name,size)) return reply_error(out,cap,400,"Unsupported disk size. Use a 140 KB sector image, 232960-byte NIB, or a compatible 5.25-inch WOZ/BDSK. Hard disks and 3.5-inch images are not supported here.");
    if(f_mkdir("/apple")!=FR_OK) { FILINFO d; if(f_stat("/apple",&d)!=FR_OK || !(d.fattrib&AM_DIR)) return reply_error(out,cap,503,"Storage is not ready. Insert a formatted SD card or prepare the badge data volume."); }
    if(!target_available(name)) return reply_error(out,cap,409,"That name already exists, or has saved changes. Choose a new name to keep both copies.");
    // A previous power loss may leave our fixed staging file. It is never listed.
    FRESULT fr=f_unlink(PART);
    if(fr!=FR_OK && fr!=FR_NO_FILE) return reply_error(out,cap,500,"Could not clear the interrupted upload. Check storage.");
    uint64_t space;
    if(!free_space(&space)) return reply_error(out,cap,503,"Could not read storage space.");
    if(space<(uint64_t)size+RESERVE) return reply_error(out,cap,507,"Not enough free space. Keep at least 512 KB free for saved changes and disk operations.");
    fr=f_open(&upload.file,PART,FA_CREATE_NEW|FA_WRITE|FA_READ);
    if(fr!=FR_OK) return reply_error(out,cap,500,"Could not create the upload file.");
    upload.active=true; upload.discarding=false; upload.id=++next_id; if(!upload.id) upload.id=++next_id;
    upload.size=size; upload.received=0; upload.buffered=0; upload.touched=time_us_64();
    strcpy(upload.name,name); progress(out,cap); return 201;
}
int library_chunk(uint32_t id,uint32_t offset,const uint8_t *data,size_t n,char *out,size_t cap) {
    if(!upload.active || upload.discarding || upload.id!=id) return reply_error(out,cap,409,"Upload expired or belongs to another session. Choose the file again.");
    if(!n || n>LIBRARY_CHUNK || offset!=upload.received || n>upload.size-upload.received)
        return reply_error(out,cap,409,"Upload position or size did not match. Cancel and retry; no existing disk was replaced.");
    size_t consumed=0;
    while(consumed<n) {
        size_t take=sizeof(upload.buffer)-upload.buffered;
        if(take>n-consumed) take=n-consumed;
        memcpy(upload.buffer+upload.buffered,data+consumed,take);
        upload.buffered+=take; consumed+=take;
        if(upload.buffered==sizeof(upload.buffer)) {
            UINT written=0;
            if(f_write(&upload.file,upload.buffer,sizeof(upload.buffer),&written)!=FR_OK || written!=sizeof(upload.buffer)) {
                library_shutdown(); return reply_error(out,cap,507,"Storage write failed or the disk is full. The incomplete upload was discarded.");
            }
            upload.buffered=0;
        }
    }
    upload.received+=(uint32_t)n; upload.touched=time_us_64(); return progress(out,cap);
}
static bool read_at(uint32_t off,void *p,UINT n) {
    UINT got=0;
    return f_lseek(&upload.file,off)==FR_OK && f_read(&upload.file,p,n,&got)==FR_OK && got==n;
}
static bool validate_image(void) {
    uint8_t head[12];
    const char *e=extension(upload.name);
    if(!strcasecmp(e,".bdsk")) {
        if(!read_at(0,head,8) || memcmp(head,"BDSK",4) || le16(head+4)!=1 || le16(head+6)!=35) return false;
        for(unsigned i=0;i<35;i++) if(!read_at(8+i*6660,head,4) || le32(head)<100 || le32(head)>6656u*8) return false;
    } else if(!strcasecmp(e,".woz")) {
        if(!read_at(0,head,12) || (memcmp(head,"WOZ1",4) && memcmp(head,"WOZ2",4)) || memcmp(head+4,"\xff\x0a\x0d\x0a",4)) return false;
        bool v2=head[3]=='2', info=false;
        uint32_t tmap=0,trks=0,trks_size=0,off=12;
        while(off<upload.size) {
            if(upload.size-off<8 || !read_at(off,head,8)) return false;
            uint32_t n=le32(head+4); off+=8;
            if(n>upload.size-off) return false;
            if(!memcmp(head,"INFO",4)) {
                if(info || n<60 || !read_at(off,head,2) || head[1]!=1) return false;
                info=true;
            } else if(!memcmp(head,"TMAP",4)) { if(tmap || n!=160) return false; tmap=off; }
            else if(!memcmp(head,"TRKS",4)) { if(trks) return false; trks=off; trks_size=n; }
            off+=n;
        }
        if(!info || !tmap || !trks || trks_size<(v2?1280u:35u*6656u)) return false;
        uint8_t map[160]; if(!read_at(tmap,map,160)) return false;
        for(unsigned i=0;i<160;i++) {
            unsigned t=map[i]; if(t==255) continue;
            // The existing emulator stores 35 tracks, including WOZ2 entries.
            if(t>=35) return false;
            if(v2) {
                if(!read_at(trks+t*8,head,8)) return false;
                uint32_t start=(uint32_t)le16(head)*512u, blocks=(uint32_t)le16(head+2)*512u, bits=le32(head+4);
                if(bits<100 || bits>6656u*8 || start<trks+1280 || start>trks+trks_size || blocks>trks+trks_size-start || (bits+7)/8>blocks) return false;
            } else {
                if(!read_at(trks+t*6656+6646,head,4)) return false;
                if(le16(head)>6646 || le16(head+2)<100 || le16(head+2)>(uint32_t)le16(head)*8) return false;
            }
        }
    }
    return true;
}
int library_finish(uint32_t id,char *out,size_t cap) {
    if(!upload.active || upload.discarding || upload.id!=id) return reply_error(out,cap,409,"Upload expired or was cancelled.");
    if(upload.received!=upload.size) return reply_error(out,cap,409,"The upload is incomplete. No disk was added.");
    UINT written=0;
    if((upload.buffered && (f_write(&upload.file,upload.buffer,upload.buffered,&written)!=FR_OK || written!=upload.buffered)) || f_sync(&upload.file)!=FR_OK) {
        library_shutdown(); return reply_error(out,cap,507,"Could not save the complete file. Check storage space.");
    }
    upload.buffered=0;
    if(!validate_image()) { library_shutdown(); return reply_error(out,cap,400,"This image is damaged or uses a disk layout this emulator cannot read. Try a standard 140 KB .dsk/.po image."); }
    FRESULT fr=f_close(&upload.file);
    if(fr!=FR_OK) { library_shutdown(); return reply_error(out,cap,500,"Storage could not close the upload. Check the device, then retry."); }
    upload.active=false;
    if(!target_available(upload.name)) { f_unlink(PART); return reply_error(out,cap,409,"Could not finish safely. A file with that name may already exist; choose another name."); }
    char path[96]; snprintf(path,sizeof(path),"/apple/%s",upload.name);
    if(f_rename(PART,path)!=FR_OK) { f_unlink(PART); return reply_error(out,cap,500,"Could not finish the upload. Check storage."); }
    snprintf(out,cap,"{\"name\":\"%s\",\"size\":%lu}",upload.name,(unsigned long)upload.size); return 201;
}
int library_cancel(uint32_t id,char *out,size_t cap) {
    if(upload.active && upload.id!=id) return reply_error(out,cap,409,"That upload belongs to another session.");
    library_shutdown(); if(upload.active) return reply_error(out,cap,503,"Storage is not responding. Cleanup will retry when it recovers."); snprintf(out,cap,"{}"); return 200;
}
// Filenames we accept cannot contain JSON quotes, backslashes or control bytes.
int library_list(unsigned offset,char *out,size_t cap) {
    if(cap<256) return reply_error(out,cap,500,"Disk-list response buffer is too small.");
    DIR dir; FILINFO file; uint64_t space=0;
    if(!free_space(&space) || f_opendir(&dir,"/apple")!=FR_OK) return reply_error(out,cap,503,"Storage is not ready. Prepare the SD card or badge data volume first.");
    size_t used=(size_t)snprintf(out,cap,"{\"free\":%llu,\"uploading\":%s,\"files\":[",(unsigned long long)space,upload.active?"true":"false");
    unsigned seen=0,count=0; bool more=false; FRESULT fr;
    while((fr=f_readdir(&dir,&file))==FR_OK && file.fname[0]) {
        if((file.fattrib&AM_DIR) || !library_name_valid(file.fname)) continue;
        // Hide automatic working sidecars when their original is still present.
        if(!strcasecmp(extension(file.fname),".bdsk")) {
            char original[96]; FILINFO fi;
            snprintf(original,sizeof(original),"/apple/%s",file.fname); original[strlen(original)-5]=0;
            if(f_stat(original,&fi)==FR_OK) continue;
        }
        if(seen++<offset) continue;
        if(count==24 || used+128>=cap) { more=true; break; }
        used+=(size_t)snprintf(out+used,cap-used,"%s{\"name\":\"%s\",\"size\":%lu}",count?",":"",file.fname,(unsigned long)file.fsize); count++;
    }
    f_closedir(&dir);
    if(fr!=FR_OK) return reply_error(out,cap,500,"Could not read the disk list.");
    snprintf(out+used,cap-used,"],\"next\":%u}",more?offset+count:0); return 200;
}
