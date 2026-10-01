// SPDX-License-Identifier: MIT
#include "dos_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t disk[35][16][256];
static bool readable = true;
static bool read_sector(void *context, uint8_t t, uint8_t s, uint8_t out[256]) {
    (void)context; assert(t < 35 && s < 16);
    memcpy(out, disk[t][s], 256); return readable;
}
static void entry(int sector, int index, uint8_t type, const char *name) {
    uint8_t *p = disk[17][sector] + 11 + index*35;
    p[0] = 20; p[1] = 0; p[2] = type; memset(p+3,0xa0,30);
    for (size_t i=0; i<strlen(name); ++i) p[3+i] = name[i] | 0x80;
}
static void catalog_tests(void) {
    dos_program_t programs[DOS_PROGRAM_MAX]; char command[64];
    uint8_t *vtoc = disk[17][0];
    vtoc[1]=17; vtoc[2]=15; vtoc[3]=3; vtoc[0x27]=122;
    vtoc[0x34]=35; vtoc[0x35]=16; vtoc[0x37]=1;
    disk[17][15][1]=17; disk[17][15][2]=14;
    entry(15,0,2,"ZEBRA"); entry(15,1,0x82,"A LOCKED PROGRAM");
    entry(15,2,4,"BINARY"); entry(15,3,1,"INTEGER");
    entry(15,4,2,"DELETED"); disk[17][15][11+4*35]=0xff;
    entry(15,5,2,"BAD:RUN"); entry(15,6,2,"BAD,D2");
    entry(14,0,2,"BRIAN'S THEME"); entry(14,1,2,"ABCDEFGHIJKLMNOPQRSTUVWXYZ1234");
    assert(dos_catalog_scan(read_sector,NULL,programs,DOS_PROGRAM_MAX)==4);
    assert(!strcmp(programs[0].name,"A LOCKED PROGRAM"));
    assert(!strcmp(programs[1].name,"ABCDEFGHIJKLMNOPQRSTUVWXYZ1234"));
    assert(!strcmp(programs[2].name,"BRIAN'S THEME"));
    assert(!strcmp(programs[3].name,"ZEBRA"));
    assert(dos_program_command(command,sizeof(command),programs[2].name,true));
    assert(!strcmp(command,"RUN BRIAN'S THEME,S6,D1\r"));
    assert(dos_program_command(command,sizeof(command),programs[1].name,false));
    assert(!strncmp(command,"LOAD ",5));
    assert(!dos_program_command(command,4,"HELLO",true) && !command[0]);
    assert(!dos_program_command(command,sizeof(command),"X\rDELETE HELLO",true));
    assert(!dos_program_command(command,sizeof(command),"X,D2",true));
    assert(!dos_program_command(command,sizeof(command),"",true));
    assert(dos_catalog_scan(read_sector,NULL,programs,2)==-1);
    disk[17][14][1]=17; disk[17][14][2]=15; // catalog cycle
    assert(dos_catalog_scan(read_sector,NULL,programs,DOS_PROGRAM_MAX)==-1);
    disk[17][14][1]=35;
    assert(dos_catalog_scan(read_sector,NULL,programs,DOS_PROGRAM_MAX)==-1);
    disk[17][14][1]=disk[17][14][2]=0;
    readable=false; assert(dos_catalog_scan(read_sector,NULL,programs,DOS_PROGRAM_MAX)==-1); readable=true;
    vtoc[0x35]=13; assert(dos_catalog_scan(read_sector,NULL,programs,DOS_PROGRAM_MAX)==-1); vtoc[0x35]=16;
    memset(disk[17][15],0,256);
    assert(dos_catalog_scan(read_sector,NULL,programs,DOS_PROGRAM_MAX)==0);
}

static uint8_t track_bits[6656];
static unsigned bit_count;
static void put(unsigned value, unsigned n) {
    for (int i=(int)n-1; i>=0; --i) {
        assert(bit_count < sizeof(track_bits)*8);
        if ((value>>i)&1) track_bits[bit_count/8] |= 1u<<(7-bit_count%8);
        bit_count++;
    }
}
static const uint8_t gcr[] = {
    0x96,0x97,0x9a,0x9b,0x9d,0x9e,0x9f,0xa6,0xa7,0xab,0xac,0xad,0xae,0xaf,0xb2,0xb3,
    0xb4,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xcb,0xcd,0xce,0xcf,0xd3,
    0xd6,0xd7,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe5,0xe6,0xe7,0xe9,0xea,0xeb,0xec,
    0xed,0xee,0xef,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
};
static void encode(const uint8_t data[256], unsigned physical, bool gaps, bool bad_checksum) {
    memset(track_bits,0,sizeof(track_bits)); bit_count=0;
    for (int i=0;i<20;i++) put(0x3fc,10);
    put(0xd5aa96,24);
    uint8_t header[]={254,17,physical,254^17^physical};
    for (int i=0;i<4;i++) { put((header[i]>>1)|0xaa,8); put(header[i]|0xaa,8); }
    put(0xdeaaeb,24);
    for (int i=0;i<6;i++) put(0x3fc,10);
    put(0xd5aaad,24);
    uint8_t six[342]={0};
    for (unsigned i=0;i<256;i++) {
        six[i+86]=data[i]>>2;
        six[i%86] |= (((data[i]&1)<<1)|((data[i]>>1)&1)) << ((i/86)*2);
    }
    uint8_t previous=0;
    for (int i=0;i<342;i++) {
        if (gaps && i%7==0) put(0,2);
        put(gcr[previous^six[i]],8); previous=six[i];
    }
    put(gcr[previous^(bad_checksum?1:0)],8); put(0xdeaaeb,24);
    for (int i=0;i<40;i++) put(0x3fc,10);
}
static void sector_tests(void) {
    const uint8_t map[]={0,7,14,6,13,5,12,4,11,3,10,2,9,1,8,15};
    uint8_t expected[256], result[256], rotated[6656];
    for (int i=0;i<256;i++) expected[i]=(i*37+5)&255;
    for (unsigned s=0;s<16;s++) for(int gaps=0;gaps<2;gaps++) {
        encode(expected,s,gaps,false);
        assert(dos_catalog_sector(track_bits,bit_count,17,map[s],result));
        assert(!memcmp(expected,result,256));
        // Header and data wrap the end of a circular track at different bit alignments.
        for(unsigned shift=1;shift<bit_count;shift+=577) {
            memset(rotated,0,sizeof(rotated));
            for(unsigned i=0;i<bit_count;i++) {
                unsigned src=(i+shift)%bit_count;
                if((track_bits[src/8]>>(7-src%8))&1) rotated[i/8]|=1u<<(7-i%8);
            }
            assert(dos_catalog_sector(rotated,bit_count,17,map[s],result));
            assert(!memcmp(expected,result,256));
        }
        assert(!dos_catalog_sector(track_bits,bit_count,16,map[s],result));
        assert(!dos_catalog_sector(track_bits,bit_count,17,map[s]^1,result));
    }
    encode(expected,0,false,true);
    assert(!dos_catalog_sector(track_bits,bit_count,17,0,result));
    memset(track_bits,0,sizeof(track_bits));
    assert(!dos_catalog_sector(track_bits,sizeof(track_bits)*8,17,0,result));
    assert(!dos_catalog_sector(track_bits,0,17,0,result));
    assert(!dos_catalog_sector(track_bits,sizeof(track_bits)*8+1,17,0,result));
}
static uint8_t *bdsk;
static bool read_bdsk(void *ctx,uint8_t t,uint8_t s,uint8_t out[256]) {
    (void)ctx; unsigned offset=8+t*6660; uint32_t count;
    memcpy(&count,bdsk+offset,4);
    return dos_catalog_sector(bdsk+offset+4,count,t,s,out);
}
int main(int argc,char **argv) {
    catalog_tests(); sector_tests();
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb"); assert(f);
        bdsk=malloc(233108); assert(bdsk);
        assert(fread(bdsk,1,233108,f)==233108); fclose(f);
        dos_program_t programs[DOS_PROGRAM_MAX];
        int n=dos_catalog_scan(read_bdsk,NULL,programs,DOS_PROGRAM_MAX); assert(n>0);
        printf("Read %d programs from saved BDSK: ",n);
        for(int i=0;i<n;i++) printf("%s%s",i?", ":"",programs[i].name);
        puts(""); free(bdsk);
    }
    puts("DOS catalog: filtering, commands, bounds, cycles, checksums, all sectors, circular/unaligned tracks PASS");
}
