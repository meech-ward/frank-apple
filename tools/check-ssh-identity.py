#!/usr/bin/env python3
"""Exercise durable SSH host-key creation, power-loss recovery and storage errors."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
fixture = r'''
#include "ssh_identity.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned random_calls, writes;
static bool random_fail, sync_fail, rename_fail, read_fail;
static unsigned short_write;
static uint8_t files[2][100];
static unsigned sizes[2];
static bool exists[2];
static int index_for(const char *path) {
    if(!strcmp(path,"/ssh.key")) return 0;
    assert(!strcmp(path,"/ssh-key.tmp")); return 1;
}
FRESULT f_open(FIL *f,const char *path,unsigned mode) {
    int i=index_for(path); f->index=i;
    if(mode==FA_READ) return exists[i]?FR_OK:FR_NO_FILE;
    assert(mode==(FA_WRITE|FA_CREATE_ALWAYS)); exists[i]=true;sizes[i]=0;return FR_OK;
}
FRESULT f_read(FIL *f,void *data,UINT cap,UINT *n) {
    if(read_fail) return FR_DISK_ERR;
    *n=sizes[f->index]<cap?sizes[f->index]:cap;memcpy(data,files[f->index],*n);return FR_OK;
}
FRESULT f_write(FIL *f,const void *data,UINT n,UINT *written) {
    writes++;*written=short_write?short_write:n;assert(*written<=100);
    memcpy(files[f->index],data,*written);sizes[f->index]=*written;return FR_OK;
}
FRESULT f_sync(FIL *f) { (void)f;return sync_fail?FR_DISK_ERR:FR_OK; }
FRESULT f_close(FIL *f) { (void)f;return FR_OK; }
FRESULT f_rename(const char *from,const char *to) {
    int i=index_for(from),j=index_for(to);assert(exists[i]&&!exists[j]);
    if(rename_fail) return FR_DISK_ERR;
    memcpy(files[j],files[i],sizeof(files[i]));sizes[j]=sizes[i];exists[j]=true;exists[i]=false;return FR_OK;
}
static bool random_bytes(uint8_t *out,size_t n) {
    random_calls++;assert(n==32);for(size_t i=0;i<n;i++)out[i]=(uint8_t)(i+random_calls);return !random_fail;
}
static void reset(void) {
    memset(exists,0,sizeof(exists));memset(sizes,0,sizeof(sizes));
    random_calls=writes=short_write=0;random_fail=sync_fail=rename_fail=read_fail=false;
}
static void failure(uint8_t seed[32]) {
    memset(seed,0xff,32);assert(!ssh_identity_load(seed,random_bytes));assert(ssh_identity_error());
    for(unsigned i=0;i<32;i++)assert(seed[i]==0);
}
int main(void) {
    uint8_t seed[32],saved[32];reset();
    assert(ssh_identity_load(seed,random_bytes));memcpy(saved,seed,32);
    assert(random_calls==1&&writes==1&&exists[0]&&!exists[1]&&!ssh_identity_error());
    assert(ssh_identity_load(seed,random_bytes)&&!memcmp(seed,saved,32)&&random_calls==1&&writes==1);
    files[0][15]^=1;failure(seed);assert(random_calls==1&&writes==1);
    files[0][15]^=1;sizes[0]++;failure(seed);assert(writes==1);
    reset();rename_fail=true;failure(seed);assert(exists[1]&&!exists[0]);
    rename_fail=false;assert(ssh_identity_load(seed,random_bytes)&&random_calls==1&&writes==1);
    reset();short_write=12;failure(seed);short_write=0;
    assert(ssh_identity_load(seed,random_bytes)&&random_calls==2&&writes==2);
    reset();sync_fail=true;failure(seed);assert(!exists[0]);
    sync_fail=false;assert(ssh_identity_load(seed,random_bytes)&&random_calls==1);
    read_fail=true;failure(seed);assert(random_calls==1);
    reset();random_fail=true;failure(seed);assert(!exists[0]&&!exists[1]&&!writes);
    puts("PASS: stable SSH identity, corruption rejection, first-write recovery, storage/entropy failures.");
}
'''
with tempfile.TemporaryDirectory(prefix='apple2-ssh-key-') as directory:
    p = Path(directory)
    (p/'ff.h').write_text('''#include <stdbool.h>
typedef unsigned UINT; typedef int FRESULT; typedef struct { int index; } FIL;
enum {FR_OK,FR_NO_FILE,FR_NO_PATH,FR_DISK_ERR};
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 8
FRESULT f_open(FIL *,const char *,unsigned);FRESULT f_read(FIL *,void *,UINT,UINT *);
FRESULT f_write(FIL *,const void *,UINT,UINT *);FRESULT f_close(FIL *);FRESULT f_sync(FIL *);
FRESULT f_rename(const char *,const char *);
''')
    (p/'check.c').write_text(fixture)
    exe = p/'check'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g',
                    '-fsanitize=address,undefined','-I',str(p),'-I',str(root/'src'),
                    str(p/'check.c'),str(root/'src/ssh_identity.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
