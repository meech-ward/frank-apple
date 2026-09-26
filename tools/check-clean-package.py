#!/usr/bin/env python3
"""Mount the generated empty/private data volumes with the firmware's actual FatFs."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('package_clean',root/'tools/package-clean.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
fixture=r'''
int main(int argc,char **argv){
 assert(argc==3);FILE *input=fopen(argv[1],"rb");assert(input);assert(fread(media,1,sizeof(media),input)==sizeof(media));assert(fgetc(input)==EOF);fclose(input);
 FATFS fs;assert(f_mount(&fs,"",1)==FR_OK);DIR dir;FILINFO info;
 assert(f_opendir(&dir,"/apple")==FR_OK);assert(f_readdir(&dir,&info)==FR_OK&&!info.fname[0]);assert(f_closedir(&dir)==FR_OK);
 assert(f_opendir(&dir,"/")==FR_OK);unsigned entries=0;while(f_readdir(&dir,&info)==FR_OK&&info.fname[0]){assert(!strcmp(info.fname,"APPLE")||!strcmp(info.fname,"WIFI.INI"));entries++;}assert(entries==1u+(unsigned)atoi(argv[2]));assert(f_closedir(&dir)==FR_OK);
 FIL f;BYTE data[8];UINT n;
 if(atoi(argv[2])){assert(f_open(&f,"/wifi.ini",FA_READ)==FR_OK);assert(f_read(&f,data,7,&n)==FR_OK&&n==7&&!memcmp(data,"[wifi]\n",7));assert(f_close(&f)==FR_OK);}
 assert(f_open(&f,"/apple/My Disk.dsk",FA_CREATE_NEW|FA_WRITE)==FR_OK);assert(f_write(&f,"test",4,&n)==FR_OK&&n==4);assert(f_close(&f)==FR_OK);
 assert(f_open(&f,"/apple/My Disk.dsk",FA_READ)==FR_OK);assert(f_read(&f,data,4,&n)==FR_OK&&n==4&&!memcmp(data,"test",4));assert(f_close(&f)==FR_OK);
 puts("PASS: generated FAT16 volume mounts, contains no application disks, and supports new files.");
}
'''
with tempfile.TemporaryDirectory(prefix='apple2-package-') as directory:
 p=Path(directory);(p/'pico').mkdir();(p/'pico/time.h').write_text('#include <stdint.h>\nuint64_t time_us_64(void);\n')
 (p/'check.c').write_text('#define main storage_tests_main\n#include "'+str(root/'tools/test-disk-library.c')+'"\n#undef main\n'+fixture)
 fat=root/'drivers/fatfs'
 subprocess.run(['cc','-std=c11','-g','-fsanitize=address,undefined','-I',str(p),'-I',str(root/'src'),'-I',str(fat),str(p/'check.c'),str(root/'src/disk_library.c'),*[str(fat/n) for n in ['ff.c','ffsystem.c','ffunicode.c']],'-o',str(p/'check')],check=True)
 for wifi in [None,b'[wifi]\nssid=Example\npassword=ExamplePassword\n']:
  image=p/'data.img';image.write_bytes(module.empty_volume(wifi));subprocess.run([str(p/'check'),str(image),'1' if wifi else '0'],check=True)
