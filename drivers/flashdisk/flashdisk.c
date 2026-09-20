// STUB: replaced by the real driver (worker package). Lets the tree link.
#include "flashdisk.h"
#include "ff.h"
#include "diskio.h"
#include "../../src/board_config.h"

uint32_t flashdisk_offset(void) { return FLASHDISK_OFFSET; }
uint32_t flashdisk_size(void) { return FLASHDISK_SIZE; }
bool flashdisk_ready(void) { return false; }
uint32_t flashdisk_erase_count(void) { return 0; }

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return STA_NOINIT; }
DSTATUS disk_status(BYTE pdrv) { (void)pdrv; return STA_NOINIT; }
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) { (void)pdrv; (void)buff; (void)sector; (void)count; return RES_NOTRDY; }
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) { (void)pdrv; (void)buff; (void)sector; (void)count; return RES_NOTRDY; }
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) { (void)pdrv; (void)cmd; (void)buff; return RES_NOTRDY; }
DWORD get_fattime(void) { return ((DWORD)(2026 - 1980) << 25) | ((DWORD)9 << 21) | ((DWORD)20 << 16); }
