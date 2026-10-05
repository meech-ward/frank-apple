// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RAM_PAGES_PER_POOL 16
#define RAM_PAGE_SIZE 4096
#define NETCARD_WEB_CONTROL 1
#ifndef NETCARD_SSH
#define NETCARD_SSH 1
#endif
#define BOARD_TUFTY 1
#define __scratch_x()
#define __dmb() ((void)0)
#define FRANK_LED_PUT(x) ((void)(x))
#define MII_DEBUG_PRINTF(...) ((void)0)
#define MII_BANK_SW 0
#define SWKBD 0xc000
#define SWAKD 0xc010
#define SWINTCXROMOFF 0xc006
typedef int mutex_t;
static inline void mutex_enter_blocking(mutex_t *m) { (void)m; }
static inline void mutex_exit(mutex_t *m) { (void)m; }
typedef struct { int unused; } FIL;
typedef unsigned UINT;
typedef int FRESULT;
#define FR_OK 0
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 8
FRESULT f_open(FIL *,const char *,unsigned);
FRESULT f_close(FIL *);
FRESULT f_read(FIL *,void *,UINT,UINT *);
FRESULT f_write(FIL *,const void *,UINT,UINT *);
FRESULT f_unlink(const char *);
typedef struct { uint8_t unused; } mii_bank_t;
typedef struct mii_t { mii_bank_t bank[1]; } mii_t;
void mii_reset(mii_t *,bool);
void mii_bank_poke(mii_bank_t *,unsigned,uint8_t);
void mii_mem_access(mii_t *,unsigned,uint8_t *,bool,bool);
void web_control_toggle(void);
bool web_control_enabled(void);
void web_control_address(char *,size_t);
