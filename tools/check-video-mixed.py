#!/usr/bin/env python3
"""Exercise the actual badge graphics renderers with synthetic RAM and character ROM.

Checks pixels at the graphics/text boundary, all four text rows, and full-screen
graphics. Optional source path allows checking the same fixture against an older revision.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source_path = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    Path(__file__).resolve().parents[1] / "src/mii_video.c"
)
source = source_path.read_text()


def function(name):
    start = source.index(name + "(")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return "static void\n" + source[start:end] + "\n"


fixture = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define MII_BANK_MAIN 0
#define MII_VIDEO_BANK 1
enum { SWTEXT, SWMIXED, SWHIRES, SWPAGE2, SW80COL, SWDHIRES, SW80STORE, SWALTCHARSET };
#define SWW_GETSTATE(sw, bit) (((sw) >> (bit)) & 1)
#define M_SWTEXT (1 << SWTEXT)
#define M_SWMIXED (1 << SWMIXED)
#define M_SWHIRES (1 << SWHIRES)
#define M_SWPAGE2 (1 << SWPAGE2)
#define M_SW80COL (1 << SW80COL)
#define M_SWDHIRES (1 << SWDHIRES)
typedef struct { uint8_t rom[8192]; unsigned len; } rom_t;
typedef struct { rom_t *rom; unsigned rom_bank, frame_count, an3_mode; } mii_video_t;
typedef struct { struct { void *vram_desc; } ua; uint8_t data[65536]; } mii_bank_t;
typedef struct { uint32_t sw_state; mii_bank_t bank[2]; mii_video_t video; } mii_t;
static uint8_t line_buffer[160];
static int lock_y = -1;
static void pin_ram_pages_for(void *p, unsigned a, unsigned n) {}
static void mii_bank_read(mii_bank_t *b, unsigned a, void *out, unsigned n) {
    memcpy(out, b->data + a, n);
}
static void tight_loop_contents(void) {}
static void sleep_ms(unsigned ms) {}
static bool input_speed_visible(void) { return false; }
static int mii_disk2_get_motor_state(void) { return 0; }
static void mii_video_draw_floppy_indicator(uint8_t *fb, int motor, unsigned frame) {}
static void mii_video_render_text40_rp2350(mii_t *m, uint8_t *fb, int w) {
    memset(fb, 0x77, 240 * 160);
}
static void mii_video_render_hires_rp2350(mii_t *m, uint8_t *fb, int w) {
    memset(fb + 24 * 160, 0x22, 192 * 160);
}
static void mii_video_render_dhires_rp2350(mii_t *m, uint8_t *fb, int w) {
    mii_video_render_hires_rp2350(m, fb, w);
}
'''
for name in ("mii_video_render_text40_mixed_rp2350",
             "mii_video_render_lores_rp2350", "mii_video_scale_to_hdmi"):
    fixture += function(name)
fixture += r'''
static unsigned pixel(const uint8_t *fb, int x, int y) {
    return (fb[y * 160 + x / 2] >> (4 * (x & 1))) & 15;
}
int main(void) {
    static mii_t m;
    static rom_t rom;
    static uint8_t guarded[240 * 160 + 2];
    uint8_t *fb = guarded + 1;
    guarded[0] = 0x5a; guarded[sizeof(guarded) - 1] = 0xa5;
    rom.len = sizeof(rom.rom); m.video.rom = &rom;
    memset(rom.rom, 0xff, sizeof(rom.rom));
    for (int cy = 0; cy < 8; cy++) rom.rom[0xa1 * 8 + cy] = ~(1 << (cy % 7));
    for (int row = 0; row < 24; row++) {
        unsigned addr = 0x400 + (row & 7) * 128 + (row / 8) * 40;
        memset(m.bank[0].data + addr, row < 20 ? 0x66 : 0xa0, 40);
        memset(m.bank[1].data + addr, 0xa0, 40);
        if (row >= 20) m.bank[0].data[addr] = m.bank[1].data[addr] = 0xa1;
    }
    m.sw_state = M_SWMIXED;
    mii_video_scale_to_hdmi(&m.video, fb);
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) assert(pixel(fb, x, y) == 6);
    for (int y = 200; y < 240; y++) {
        int glyph_x = (((y - 200) % 10) * 8 / 10) % 7;
        for (int x = 0; x < 320; x++)
            assert(pixel(fb, x, y) == (x == glyph_x ? 15 : 0));
    }
    m.sw_state |= M_SW80COL;
    rom.rom[0xa1 * 8] = 0xfc; /* A two-pixel stroke survives 80-column compression. */
    mii_video_scale_to_hdmi(&m.video, fb);
    assert(pixel(fb, 0, 200) == 15 && pixel(fb, 4, 200) == 15);
    assert(pixel(fb, 8, 200) == 0 && pixel(fb, 319, 239) == 0);
    rom.rom[0xa1 * 8] = 0xfe;

    m.sw_state = 0; /* Full-screen lores must retain all 48 graphics rows. */
    mii_video_scale_to_hdmi(&m.video, fb);
    assert(pixel(fb, 0, 200) == 1 && pixel(fb, 8, 200) == 0);
    assert(pixel(fb, 8, 205) == 10 && pixel(fb, 319, 239) == 10);

    m.sw_state = M_SWMIXED | M_SWHIRES;
    mii_video_scale_to_hdmi(&m.video, fb);
    assert(pixel(fb, 0, 23) == 0 && pixel(fb, 0, 183) == 2);
    for (int y = 184; y < 216; y++)
        for (int x = 0; x < 320; x++)
            assert(pixel(fb, x, y) == (x == (y - 184) % 8 % 7 ? 15 : 0));
    assert(pixel(fb, 0, 216) == 0);
    m.sw_state = M_SWHIRES;
    mii_video_scale_to_hdmi(&m.video, fb);
    assert(pixel(fb, 0, 184) == 2 && pixel(fb, 319, 215) == 2);
    m.sw_state = M_SWTEXT | M_SWMIXED;
    mii_video_scale_to_hdmi(&m.video, fb);
    assert(pixel(fb, 0, 200) == 7 && pixel(fb, 319, 239) == 7);
    assert(guarded[0] == 0x5a && guarded[sizeof(guarded) - 1] == 0xa5);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="badge-video-") as directory:
    p = Path(directory)
    (p / "check.c").write_text(fixture)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-Wno-unused-variable", str(p / "check.c"), "-o", str(p / "check")], check=True)
    subprocess.run([str(p / "check")], check=True)
print("PASS: mixed lores and hires text, 80-column lores text, full graphics, text mode, and buffer bounds.")
