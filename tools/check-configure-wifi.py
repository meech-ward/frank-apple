#!/usr/bin/env python3
"""Test non-destructive Wi-Fi image edits with real FatFs and simulated USB IO."""
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


updater = module("configure_wifi", root / "tools/configure-wifi.py")
package = module("package_clean", root / "tools/package-clean.py")
fixture = r'''
#define main image_editor_main
#include "wifi-image-edit.c"
#undef main
#include <assert.h>
static BYTE payload[143360];
static const char *names[]={"1:/apple/Saved Program.dsk","1:/apple/Working copy.dsk.bdsk","1:/notes/Read me.txt","1:/ssh.key"};
static const UINT sizes[]={143360,4096,137,44};
static void save(const char *path){FILE *out=fopen(path,"wb");assert(out);assert(fwrite(media[1],1,IMAGE_SIZE,out)==IMAGE_SIZE);assert(!fclose(out));}
int main(int argc,char **argv){
 assert(argc==4);FILE *input=fopen(argv[2],"rb");assert(input);assert(fread(media[1],1,IMAGE_SIZE,input)==IMAGE_SIZE);assert(!fclose(input));
 FATFS fs;assert(f_mount(&fs,"1:",1)==FR_OK);
 for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(BYTE)((i*17u)^(i>>8));
 if(!strcmp(argv[1],"create")){
  assert(f_mkdir("1:/notes")==FR_OK);
  for(unsigned i=0;i<4;i++){FIL f;UINT count;assert(f_open(&f,names[i],FA_CREATE_NEW|FA_WRITE)==FR_OK);assert(f_write(&f,payload,sizes[i],&count)==FR_OK&&count==sizes[i]);assert(f_close(&f)==FR_OK);}
  save(argv[3]);
 }else if(!strcmp(argv[1],"full")){
  FIL f;UINT count;assert(f_open(&f,"1:/filler",FA_CREATE_NEW|FA_WRITE)==FR_OK);
  do{assert(f_write(&f,payload,sizeof(payload),&count)==FR_OK);}while(count==sizeof(payload));
  assert(f_close(&f)==FR_OK);save(argv[3]);
 }else{
  assert(!strcmp(argv[1],"check"));
  for(unsigned i=0;i<4;i++){FIL f;UINT count;BYTE readback[143360];assert(f_open(&f,names[i],FA_READ)==FR_OK&&f_size(&f)==sizes[i]);assert(f_read(&f,readback,sizeof(readback),&count)==FR_OK&&count==sizes[i]);assert(!memcmp(readback,payload,count));assert(f_close(&f)==FR_OK);}
  char expected[WIFI_CONFIG_MAX+1];size_t size;assert(read_config(argv[3],expected,&size));FIL f;UINT count;char got[WIFI_CONFIG_MAX+1];
  assert(f_open(&f,"1:/wifi.ini",FA_READ)==FR_OK&&f_size(&f)==size);assert(f_read(&f,got,sizeof(got),&count)==FR_OK&&count==size&&!memcmp(got,expected,size));assert(f_close(&f)==FR_OK);
 }
 assert(f_mount(NULL,"1:",0)==FR_OK);
}
'''
fake_picotool = r'''#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
args=sys.argv[1:]
device=Path(os.environ['APPLE2_TEST_DEVICE'])
with Path(os.environ['APPLE2_TEST_LOG']).open('a') as log:log.write(json.dumps(args)+'\n')
assert '--ser' in args and args[args.index('--ser')+1]=='test-board'
if args[0]=='save':
 assert '-a' in args and '-v' in args
 Path(args[3]).write_bytes(device.read_bytes())
elif args[0]=='verify':
 assert args[args.index('-o')+1]=='0x10000000'
 assert Path(args[1]).read_bytes()==device.read_bytes()
elif args[0]=='load':
 assert '--ignore-partitions' in args and '-v' in args
 offset=int(args[args.index('-o')+1],16)-0x10000000
 data=Path(args[3]).read_bytes();assert offset>=4*1024*1024 and offset%4096==0 and len(data)%4096==0 and offset+len(data)<=16*1024*1024
 current=bytearray(device.read_bytes());current[offset:offset+len(data)]=data;device.write_bytes(current)
 if os.environ.get('APPLE2_TEST_FAIL_LOAD'):sys.exit('simulated interrupted load')
else:raise AssertionError(args)
'''

with tempfile.TemporaryDirectory(prefix="apple2-config-test-") as directory:
    p = Path(directory)
    helper = updater.build_helper(p, sanitize=True)
    (p / "fixture.c").write_text(fixture)
    fat = root / "drivers/fatfs"
    subprocess.run([
        "cc", "-std=c11", "-g", "-fsanitize=address,undefined", "-I", str(root / "src"),
        "-I", str(root / "tools"), "-I", str(fat), str(p / "fixture.c"),
        str(root / "src/wifi_config.c"), *[str(fat / name) for name in ("ff.c", "ffsystem.c", "ffunicode.c")],
        "-o", str(p / "fixture"),
    ], check=True)
    empty, original, first, second = [p / name for name in ("empty.img", "original.img", "first.img", "second.img")]
    empty.write_bytes(package.empty_volume())
    updater.run([str(p / "fixture"), "create", str(empty), str(original)])
    original_hash = hashlib.sha256(original.read_bytes()).hexdigest()
    hotspot = p / "hotspot.ini"
    hotspot.write_text("[wifi]\nmode=hotspot\nssid=Portable Apple II\npassword=HotspotPassword\n[ssh]\nenabled=true\npassword=SSHPassword\n")
    station = p / "station.ini"
    station.write_text("[wifi]\nssid=Home\npassword=HomePassword\n")
    result = updater.run([str(helper), str(original), str(hotspot), str(first)])
    assert "verified 4 other files unchanged" in result
    updater.run([str(p / "fixture"), "check", str(first), str(hotspot)])
    updater.run([str(helper), str(first), str(station), str(second)])
    updater.run([str(p / "fixture"), "check", str(second), str(station)])
    assert hashlib.sha256(original.read_bytes()).hexdigest() == original_hash

    # Full-flash offline mode preserves all four MiB before the data volume.
    before = bytes(range(256)) * (updater.DATA_OFFSET // 256) + original.read_bytes()
    work = p / "offline"
    work.mkdir()
    after, _ = updater.prepare_image(before, hotspot, work, helper)
    assert after[:updater.DATA_OFFSET] == before[:updater.DATA_OFFSET]
    assert after[updater.DATA_OFFSET:] == first.read_bytes()
    ranges = updater.changed_ranges(before, after)
    assert ranges and all(start >= updater.DATA_OFFSET and start % 4096 == end % 4096 == 0 for start, end in ranges)
    rebuilt = bytearray(before)
    for start, end in ranges:
        rebuilt[start:end] = after[start:end]
    assert rebuilt == after
    try:
        updater.changed_ranges(before, b"!" + after[1:])
        raise AssertionError("firmware mutation accepted")
    except ValueError:
        pass

    # Invalid config, output collision, bad volume and full filesystem fail before output.
    bad = p / "bad.ini"
    bad.write_text("[wifi]\nmode=hotspot\nssid=Bad\npassword=\n")
    invalid_image = p / "invalid.img"
    invalid_image.write_bytes(bytes(updater.DATA_SIZE))
    full = p / "full.img"
    updater.run([str(p / "fixture"), "full", str(original), str(full)])
    for src, cfg, out in [(original, bad, p / "bad-out.img"), (invalid_image, hotspot, p / "invalid-out.img"),
                          (full, hotspot, p / "full-out.img"), (original, hotspot, first)]:
        previous = out.read_bytes() if out.exists() else None
        response = subprocess.run([str(helper), str(src), str(cfg), str(out)], capture_output=True, text=True)
        assert response.returncode != 0
        assert (out.read_bytes() if out.exists() else None) == previous
    assert hashlib.sha256(original.read_bytes()).hexdigest() == original_hash

    # USB command sequencing uses a fake picotool, never a real connected device.
    fake = p / "picotool"
    fake.write_text(fake_picotool)
    fake.chmod(0o700)
    device, log = p / "simulated-flash.bin", p / "commands.jsonl"
    device.write_bytes(before)
    os.environ.update(APPLE2_TEST_DEVICE=str(device), APPLE2_TEST_LOG=str(log))
    device_work = p / "device-work"
    device_work.mkdir()
    backup = p / "private-backup"
    with redirect_stdout(io.StringIO()):
        updater.update_device(hotspot, backup, helper, device_work, str(fake), "test-board")
    assert device.read_bytes() == after
    assert (backup / "flash-before.bin").read_bytes() == before
    assert (backup / "flash-expected.bin").read_bytes() == after
    assert backup.stat().st_mode & 0o777 == 0o700
    assert all(path.stat().st_mode & 0o777 == 0o600 for path in backup.iterdir())
    commands = [json.loads(line) for line in log.read_text().splitlines()]
    assert [args[0] for args in commands] == ["save", "verify", *(["load"] * len(ranges)), "verify"]
    assert not any(flag in args for args in commands for flag in ("-f", "-F", "-x", "reboot"))

    # A failed/interrupted write retains the original full backup and never reboots.
    device.write_bytes(before)
    log.write_text("")
    os.environ["APPLE2_TEST_FAIL_LOAD"] = "1"
    failure_work = p / "failure-work"
    failure_work.mkdir()
    failed_backup = p / "failed-backup"
    try:
        with redirect_stdout(io.StringIO()):
            updater.update_device(hotspot, failed_backup, helper, failure_work, str(fake), "test-board")
        raise AssertionError("interrupted write reported success")
    except RuntimeError:
        pass
    assert (failed_backup / "flash-before.bin").read_bytes() == before
    assert json.loads(log.read_text().splitlines()[-1])[0] == "load"
    del os.environ["APPLE2_TEST_FAIL_LOAD"]

print("PASS: real FatFs config insert/replace, saved disks/nested files/SSH key readback, firmware preservation, malformed/full images, aligned USB updates, private backups and interrupted-write retention. No USB device accessed.")
