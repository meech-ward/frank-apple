// SPDX-License-Identifier: MIT
#include "launcher_test_platform.h"
#include "disk_ui.h"
#include "disk_loader.h"
#include "typing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint8_t vram[2*RAM_PAGES_PER_POOL*RAM_PAGE_SIZE];
static uint8_t snapshot[sizeof(vram)],framebuffer[320*240/2];
FIL fp;
char selected_dir[128]="/apple";
disk_entry_t *g_disk_list=(disk_entry_t *)vram;
int g_disk_count;
loaded_disk_t g_loaded_disks[2];
static bool ready=true,fail_save,fail_restore,web;
static int catalog_count=15,stopped,scan_calls;
static char typed[128],screen[4096];

FRESULT f_open(FIL *f,const char *name,unsigned mode) { (void)f;(void)name;(void)mode;return 0; }
FRESULT f_close(FIL *f) { (void)f;return 0; }
FRESULT f_write(FIL *f,const void *data,UINT n,UINT *written) {
    (void)f; if(fail_save) return 1; assert(n==sizeof(snapshot));
    memcpy(snapshot,data,n); *written=n; return 0;
}
FRESULT f_read(FIL *f,void *data,UINT n,UINT *read) {
    (void)f; if(fail_restore) return 1; assert(n==sizeof(snapshot));
    memcpy(data,snapshot,n); *read=n; return 0;
}
FRESULT f_unlink(const char *p) { (void)p;return 1; }
bool remote_control_basic_prompt(void) { assert(vram[0]==0x6d);return ready; }
bool typing_try_literal(const uint8_t *data,size_t n) {
    assert(!disk_ui_is_visible()); assert(vram[0]==0x6d);
    assert(n<sizeof(typed)); memcpy(typed,data,n);typed[n]=0;return true;
}
void remote_control_key(uint8_t key) { assert(key==3);assert(!disk_ui_is_visible());stopped++; }
void clear_held_key(void) {}
void web_control_toggle(void) { web=!web; }
bool web_control_enabled(void) { return web; }
void web_control_address(char *out,size_t n) { snprintf(out,n,"http://10.0.0.41"); }
void mii_reset(mii_t *m,bool cold) { (void)m;(void)cold; }
void mii_bank_poke(mii_bank_t *b,unsigned a,uint8_t d) { (void)b;(void)a;(void)d; }
void mii_mem_access(mii_t *m,unsigned a,uint8_t *d,bool w,bool s) { (void)m;(void)a;(void)d;(void)w;(void)s; }
int disk_scan_directory(const char *p) {
    (void)p; scan_calls++; memset(vram,0x52,sizeof(vram));
    strcpy(g_disk_list[0].filename,"Workshop.dsk");g_disk_list[0].type=DISK_TYPE_DSK;
    g_disk_count=1;return 1;
}
bool disk_bdsk_exists2(const char *n) { (void)n;return true; }
int disk_saved_programs(mii_t *m,dos_program_t *p,size_t n) {
    (void)m;assert(disk_ui_is_visible());assert(n>=15);
    for(int i=0;i<15;i++) snprintf(p[i].name,31,"DEMO %02d",i+1);
    return catalog_count;
}
int disk_load_image(int d,int i,bool w) { (void)d;(void)i;(void)w;return 0; }
int disk_mount_to_emulator(int d,mii_t *m,int s,int p,bool r,bool b) {
    (void)d;(void)m;(void)s;(void)p;(void)r;(void)b;return 0;
}
int disk_eject_from_emulator(int d,mii_t *m,int s) { (void)d;(void)m;(void)s;return 0; }
void disk_unload_image(int d) { (void)d; }
void disk_autoboot_save(int d) { (void)d; }

static void expect(const char *s) {
    disk_ui_describe(screen,sizeof(screen));
    if(!strstr(screen,s)) { fprintf(stderr,"Expected %s in:\n%s\n",s,screen);assert(false); }
    disk_ui_render(framebuffer,320,240); // ASan also checks every state render.
}
static void key(uint8_t k) { assert(disk_ui_handle_key(k)); }
static void open_home(void) {
    memset(vram,0x6d,sizeof(vram)); typed[0]=0;
    disk_ui_show();assert(disk_ui_is_visible());expect("APPLE II");
}
static void programs_menu(void) { key(0x0a);key('\r');expect("SAVED PROGRAMS"); }
static void render_file(const char *directory,const char *name) {
    if(!directory)return;
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 240\n255\n");
    for(int i=0;i<320*240;i++) {
        unsigned v=(i&1)?(framebuffer[i/2]>>4):(framebuffer[i/2]&15);
        uint8_t rgb[3]={v*17,v*17,v*17};fwrite(rgb,1,3,f);
    }
    fclose(f);
}
int main(int argc,char **argv) {
    const char *images=argc>1?argv[1]:NULL;
    mii_t emulator={0};disk_ui_init_with_emulator(&emulator,6);
    g_loaded_disks[0].loaded=true;strcpy(g_loaded_disks[0].filename,"Workshop.dsk");
    open_home();render_file(images,"home");key(' ');assert(web);
    programs_menu();render_file(images,"programs");
    key(0x0b);expect("> DEMO 15");key(0x0a);expect("> DEMO 01");
    key('\r');expect("Replaces the program");render_file(images,"action");
    key(0x1b);expect("SAVED PROGRAMS - DRIVE 1");
    key('\r');key('\r');assert(!disk_ui_is_visible());assert(!strcmp(typed,"RUN DEMO 01,S6,D1\r"));
    for(size_t i=0;i<sizeof(vram);i++)assert(vram[i]==0x6d);
    open_home();programs_menu();key('\r');key(0x0a);key('\r');
    assert(!strcmp(typed,"LOAD DEMO 01,S6,D1\r"));
    ready=false;open_home();programs_menu();key('\r');expect("empty ] BASIC prompt");
    render_file(images,"not-ready");key(0x0a);key('\r');assert(disk_ui_is_visible()&&!typed[0]);
    key(0x1b);key(0x1b);key(0x0a);key('\r');expect("Run needs an empty");
    key(0x0a);key('\r');assert(stopped==1&&!disk_ui_is_visible());
    ready=true;open_home();key(0x0a);key(0x0a);key('\r');assert(!strcmp(typed,"RUN\r"));
    catalog_count=0;open_home();programs_menu();expect("No Applesoft programs");key('\r');assert(!typed[0]);
    catalog_count=-1;key(' ');expect("could not be read");render_file(images,"no-dos");
    key(0x1b);key(0x0b);key(0x0b);key('\r');expect("CHOOSE A DISK DRIVE");
    key(0x1b);expect("APPLE II");key('2');expect("CHOOSE A DISK FOR DRIVE 2");
    key(0x1b);key(0x1b);key(0x1b);assert(!disk_ui_is_visible());
    fail_save=true;memset(vram,0x6d,sizeof(vram));int scans=scan_calls;
    disk_ui_show();assert(!disk_ui_is_visible()&&scan_calls==scans);fail_save=false;
    catalog_count=15;open_home();programs_menu();key('\r');fail_restore=true;key('\r');
    assert(disk_ui_is_visible()&&!typed[0]);fail_restore=false;key(0x1b);key(0x1b);key(0x1b);
    assert(!disk_ui_is_visible());
    puts("Launcher: navigation, run/load, prompt guard, stop, web toggle, disk access, RAM preservation and snapshot failures PASS");
}
