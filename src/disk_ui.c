/*
 * disk_ui.c
 * 
 * Macintosh-style disk selector UI for FRANK Apple
 * Features inverted title bar, compact 6x8 font, proper selection highlighting
 */

#include "board_config.h"
#include <pico.h>
#include <pico/stdlib.h>
#include <hardware/sync.h>
#include <stdio.h>
#include <string.h>
#include "disk_ui.h"
#include "disk_loader.h"
#include "mii.h"
#include "mii_sw.h"
#include "mii_bank.h"
#include "debug_log.h"
#include "typing.h"
#if NETCARD_WEB_CONTROL
#include "web_control.h"
#endif

mutex_t video_mutex;

// External function to clear held key state (from main.c)
extern void clear_held_key(void);
bool disk_bdsk_exists2(const char *filename);

// Emulator reference (for mounting disks)
static mii_t *g_mii = NULL;
int g_disk2_slot = 6;  // Default slot for Disk II

// UI state - volatile to prevent race conditions between cores
static bool fb_needs_clear = true;
static volatile disk_ui_state_t ui_state = DISK_UI_HIDDEN;
static volatile int selected_drive = 0;      // 0 or 1
static volatile int selected_file = 0;       // Currently highlighted file
enum { DISK_BOOT, DISK_INSERT, DISK_READ_ONLY, DISK_CANCEL, DISK_ACTIONS };
static volatile int selected_action = DISK_BOOT;
static volatile int scroll_offset = 0;       // For scrolling long lists
static volatile bool ui_dirty = false;       // True when UI needs redraw
static volatile bool ui_rendered = false;    // True when UI has been rendered at least once
static int home_item, program_item, program_scroll, program_action;
static int program_count;
static dos_program_t programs[DOS_PROGRAM_MAX];
static bool basic_ready;
static const char *home_message;
static const char *home_items[] = {
    "Resume Apple II", "Saved programs", "Run program in memory",
    "Stop program (Ctrl-C)", "Choose a disk"
#if NETCARD_WEB_CONTROL
    , "Web control"
#endif
};
#define HOME_ITEMS ((int)(sizeof(home_items) / sizeof(home_items[0])))
#define PROGRAM_VISIBLE 12

static const char *home_label(int item) {
#if NETCARD_WEB_CONTROL
    if (item == 5) return web_control_enabled() ? "Web control: ON" : "Web control: OFF";
#endif
    return home_items[item];
}

// With double-buffering, the render target alternates each frame.
static uint8_t *g_last_framebuffer = NULL;

// UI dimensions - larger window with compact font
#define UI_X            24      // Left edge in 320px mode
#define UI_Y            20      // Top edge in 240px mode  
#define UI_WIDTH        272     // Dialog width
#define UI_HEIGHT       200     // Dialog height
#define UI_PADDING      6       // Padding inside dialog
#define CHAR_WIDTH      6       // Character width in pixels (compact font)
#define CHAR_HEIGHT     8       // Character height in pixels
#define HEADER_HEIGHT   12      // Title bar height
#define LINE_HEIGHT     10      // Line spacing
#define MAX_VISIBLE     17      // Max visible items
#define MAX_DISPLAY_LEN 40      // Max characters for filename display

// Colors (palette indices)
#define COLOR_BG        0   // Black
#define COLOR_BORDER    15  // White
#define COLOR_TEXT      15  // White
#define COLOR_HEADER_BG 15  // White (for inverted header)
#define COLOR_HEADER_FG 0   // Black (for inverted header)

// Compact 6x8 bitmap font (similar to Apple/Mac system font)
// Each character is 8 bytes (rows), only 6 pixels wide per row
static const uint8_t font_6x8[][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 Space
    {0x20,0x20,0x20,0x20,0x20,0x00,0x20,0x00}, // 33 !
    {0x50,0x50,0x50,0x00,0x00,0x00,0x00,0x00}, // 34 "
    {0x50,0x50,0xF8,0x50,0xF8,0x50,0x50,0x00}, // 35 #
    {0x20,0x78,0xA0,0x70,0x28,0xF0,0x20,0x00}, // 36 $
    {0xC0,0xC8,0x10,0x20,0x40,0x98,0x18,0x00}, // 37 %
    {0x40,0xA0,0xA0,0x40,0xA8,0x90,0x68,0x00}, // 38 &
    {0x20,0x20,0x40,0x00,0x00,0x00,0x00,0x00}, // 39 '
    {0x10,0x20,0x40,0x40,0x40,0x20,0x10,0x00}, // 40 (
    {0x40,0x20,0x10,0x10,0x10,0x20,0x40,0x00}, // 41 )
    {0x00,0x20,0xA8,0x70,0xA8,0x20,0x00,0x00}, // 42 *
    {0x00,0x20,0x20,0xF8,0x20,0x20,0x00,0x00}, // 43 +
    {0x00,0x00,0x00,0x00,0x00,0x20,0x20,0x40}, // 44 ,
    {0x00,0x00,0x00,0xF8,0x00,0x00,0x00,0x00}, // 45 -
    {0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00}, // 46 .
    {0x00,0x08,0x10,0x20,0x40,0x80,0x00,0x00}, // 47 /
    {0x70,0x88,0x98,0xA8,0xC8,0x88,0x70,0x00}, // 48 0
    {0x20,0x60,0x20,0x20,0x20,0x20,0x70,0x00}, // 49 1
    {0x70,0x88,0x08,0x30,0x40,0x80,0xF8,0x00}, // 50 2
    {0x70,0x88,0x08,0x30,0x08,0x88,0x70,0x00}, // 51 3
    {0x10,0x30,0x50,0x90,0xF8,0x10,0x10,0x00}, // 52 4
    {0xF8,0x80,0xF0,0x08,0x08,0x88,0x70,0x00}, // 53 5
    {0x30,0x40,0x80,0xF0,0x88,0x88,0x70,0x00}, // 54 6
    {0xF8,0x08,0x10,0x20,0x40,0x40,0x40,0x00}, // 55 7
    {0x70,0x88,0x88,0x70,0x88,0x88,0x70,0x00}, // 56 8
    {0x70,0x88,0x88,0x78,0x08,0x10,0x60,0x00}, // 57 9
    {0x00,0x00,0x20,0x00,0x00,0x20,0x00,0x00}, // 58 :
    {0x00,0x00,0x20,0x00,0x00,0x20,0x20,0x40}, // 59 ;
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x00}, // 60 <
    {0x00,0x00,0xF8,0x00,0xF8,0x00,0x00,0x00}, // 61 =
    {0x40,0x20,0x10,0x08,0x10,0x20,0x40,0x00}, // 62 >
    {0x70,0x88,0x10,0x20,0x20,0x00,0x20,0x00}, // 63 ?
    {0x70,0x88,0xB8,0xA8,0xB8,0x80,0x70,0x00}, // 64 @
    {0x70,0x88,0x88,0xF8,0x88,0x88,0x88,0x00}, // 65 A
    {0xF0,0x88,0x88,0xF0,0x88,0x88,0xF0,0x00}, // 66 B
    {0x70,0x88,0x80,0x80,0x80,0x88,0x70,0x00}, // 67 C
    {0xE0,0x90,0x88,0x88,0x88,0x90,0xE0,0x00}, // 68 D
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0xF8,0x00}, // 69 E
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0x80,0x00}, // 70 F
    {0x70,0x88,0x80,0xB8,0x88,0x88,0x70,0x00}, // 71 G
    {0x88,0x88,0x88,0xF8,0x88,0x88,0x88,0x00}, // 72 H
    {0x70,0x20,0x20,0x20,0x20,0x20,0x70,0x00}, // 73 I
    {0x38,0x10,0x10,0x10,0x90,0x90,0x60,0x00}, // 74 J
    {0x88,0x90,0xA0,0xC0,0xA0,0x90,0x88,0x00}, // 75 K
    {0x80,0x80,0x80,0x80,0x80,0x80,0xF8,0x00}, // 76 L
    {0x88,0xD8,0xA8,0xA8,0x88,0x88,0x88,0x00}, // 77 M
    {0x88,0xC8,0xA8,0x98,0x88,0x88,0x88,0x00}, // 78 N
    {0x70,0x88,0x88,0x88,0x88,0x88,0x70,0x00}, // 79 O
    {0xF0,0x88,0x88,0xF0,0x80,0x80,0x80,0x00}, // 80 P
    {0x70,0x88,0x88,0x88,0xA8,0x90,0x68,0x00}, // 81 Q
    {0xF0,0x88,0x88,0xF0,0xA0,0x90,0x88,0x00}, // 82 R
    {0x70,0x88,0x80,0x70,0x08,0x88,0x70,0x00}, // 83 S
    {0xF8,0x20,0x20,0x20,0x20,0x20,0x20,0x00}, // 84 T
    {0x88,0x88,0x88,0x88,0x88,0x88,0x70,0x00}, // 85 U
    {0x88,0x88,0x88,0x88,0x50,0x50,0x20,0x00}, // 86 V
    {0x88,0x88,0x88,0xA8,0xA8,0xD8,0x88,0x00}, // 87 W
    {0x88,0x88,0x50,0x20,0x50,0x88,0x88,0x00}, // 88 X
    {0x88,0x88,0x50,0x20,0x20,0x20,0x20,0x00}, // 89 Y
    {0xF8,0x08,0x10,0x20,0x40,0x80,0xF8,0x00}, // 90 Z
    {0x70,0x40,0x40,0x40,0x40,0x40,0x70,0x00}, // 91 [
    {0x00,0x80,0x40,0x20,0x10,0x08,0x00,0x00}, // 92 backslash
    {0x70,0x10,0x10,0x10,0x10,0x10,0x70,0x00}, // 93 ]
    {0x20,0x50,0x88,0x00,0x00,0x00,0x00,0x00}, // 94 ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF8}, // 95 _
    {0x40,0x20,0x10,0x00,0x00,0x00,0x00,0x00}, // 96 `
    {0x00,0x00,0x70,0x08,0x78,0x88,0x78,0x00}, // 97 a
    {0x80,0x80,0xB0,0xC8,0x88,0xC8,0xB0,0x00}, // 98 b
    {0x00,0x00,0x70,0x80,0x80,0x88,0x70,0x00}, // 99 c
    {0x08,0x08,0x68,0x98,0x88,0x98,0x68,0x00}, // 100 d
    {0x00,0x00,0x70,0x88,0xF8,0x80,0x70,0x00}, // 101 e
    {0x30,0x48,0x40,0xE0,0x40,0x40,0x40,0x00}, // 102 f
    {0x00,0x00,0x68,0x98,0x98,0x68,0x08,0x70}, // 103 g
    {0x80,0x80,0xB0,0xC8,0x88,0x88,0x88,0x00}, // 104 h
    {0x20,0x00,0x60,0x20,0x20,0x20,0x70,0x00}, // 105 i
    {0x10,0x00,0x30,0x10,0x10,0x90,0x60,0x00}, // 106 j
    {0x80,0x80,0x90,0xA0,0xC0,0xA0,0x90,0x00}, // 107 k
    {0x60,0x20,0x20,0x20,0x20,0x20,0x70,0x00}, // 108 l
    {0x00,0x00,0xD0,0xA8,0xA8,0xA8,0xA8,0x00}, // 109 m
    {0x00,0x00,0xB0,0xC8,0x88,0x88,0x88,0x00}, // 110 n
    {0x00,0x00,0x70,0x88,0x88,0x88,0x70,0x00}, // 111 o
    {0x00,0x00,0xB0,0xC8,0xC8,0xB0,0x80,0x80}, // 112 p
    {0x00,0x00,0x68,0x98,0x98,0x68,0x08,0x08}, // 113 q
    {0x00,0x00,0xB0,0xC8,0x80,0x80,0x80,0x00}, // 114 r
    {0x00,0x00,0x78,0x80,0x70,0x08,0xF0,0x00}, // 115 s
    {0x40,0x40,0xE0,0x40,0x40,0x48,0x30,0x00}, // 116 t
    {0x00,0x00,0x88,0x88,0x88,0x98,0x68,0x00}, // 117 u
    {0x00,0x00,0x88,0x88,0x88,0x50,0x20,0x00}, // 118 v
    {0x00,0x00,0x88,0xA8,0xA8,0xA8,0x50,0x00}, // 119 w
    {0x00,0x00,0x88,0x50,0x20,0x50,0x88,0x00}, // 120 x
    {0x00,0x00,0x88,0x88,0x98,0x68,0x08,0x70}, // 121 y
    {0x00,0x00,0xF8,0x10,0x20,0x40,0xF8,0x00}, // 122 z
    {0x10,0x20,0x20,0x40,0x20,0x20,0x10,0x00}, // 123 {
    {0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x00}, // 124 |
    {0x40,0x20,0x20,0x10,0x20,0x20,0x40,0x00}, // 125 }
    {0x00,0x00,0x40,0xA8,0x10,0x00,0x00,0x00}, // 126 ~
};

static inline void put_pixel_4bpp(uint8_t *fb, uint32_t pix_idx, uint8_t color) {
    uint8_t *p = &fb[pix_idx >> 1];
    if (pix_idx & 1) {
        // high nibble
        *p = (*p & 0x0F) | (color << 4);
    } else {
        // low nibble
        *p = (*p & 0xF0) | color;
    }
}

// Draw a filled rectangle
static void draw_rect(uint8_t *fb, int width, int x, int y, int w, int h, uint8_t color) {
    for (int dy = 0; dy < h; dy++) {
        if (y + dy < 0 || y + dy >= 240) continue;
        for (int dx = 0; dx < w; dx++) {
            if (x + dx < 0 || x + dx >= width) continue;
            put_pixel_4bpp(fb, (y + dy) * width + (x + dx), color);
        }
    }
}

// Draw a character using the 6x8 bitmap font
static void draw_char(uint8_t *fb, int fb_width, int x, int y, char c, uint8_t color) {
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx > 94) return;
    
    const uint8_t *glyph = font_6x8[idx];
    
    for (int row = 0; row < 8; row++) {
        if (y + row < 0 || y + row >= 240) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 6; col++) {
            if (x + col < 0 || x + col >= fb_width) continue;
            if (bits & (0x80 >> col)) {
                put_pixel_4bpp(fb, (y + row) * fb_width + (x + col), color);
            }
        }
    }
}

// Draw a string
void draw_string(uint8_t *fb, int fb_width, int x, int y, const char *str, uint8_t color) {
    while (*str) {
        draw_char(fb, fb_width, x, y, *str, color);
        x += CHAR_WIDTH;
        str++;
    }
}

// Draw a string with truncation and ellipsis
static void draw_string_truncated(uint8_t *fb, int fb_width, int x, int y, const char *str, int max_chars, uint8_t color) {
    int len = strlen(str);
    if (len <= max_chars) {
        draw_string(fb, fb_width, x, y, str, color);
    } else {
        // Draw truncated with "..." at end
        for (int i = 0; i < max_chars - 3; i++) {
            draw_char(fb, fb_width, x + i * CHAR_WIDTH, y, str[i], color);
        }
        draw_string(fb, fb_width, x + (max_chars - 3) * CHAR_WIDTH, y, "...", color);
    }
}

// Draw inverted header bar (Mac-style)
static void draw_header(uint8_t *fb, int fb_width, int x, int y, int w, const char *title) {
    // White background
    draw_rect(fb, fb_width, x, y, w, HEADER_HEIGHT, COLOR_HEADER_BG);
    
    // Center the title
    int title_len = strlen(title);
    int title_x = x + (w - title_len * CHAR_WIDTH) / 2;
    int title_y = y + (HEADER_HEIGHT - CHAR_HEIGHT) / 2;
    
    // Black text on white background
    draw_string(fb, fb_width, title_x, title_y, title, COLOR_HEADER_FG);
}

// Draw a menu item (with optional inversion for selection)
static void draw_menu_item(uint8_t *fb, int fb_width, int x, int y, int w, const char *text, int max_chars, bool selected) {
    if (selected) {
        // Inverted: white background, black text
        draw_rect(fb, fb_width, x, y, w, LINE_HEIGHT, COLOR_HEADER_BG);
        draw_string_truncated(fb, fb_width, x + 2, y + 1, text, max_chars, COLOR_HEADER_FG);
    } else {
        // Normal: black background, white text
        draw_rect(fb, fb_width, x, y, w, LINE_HEIGHT, COLOR_BG);
        draw_string_truncated(fb, fb_width, x + 2, y + 1, text, max_chars, COLOR_TEXT);
    }
}

// Draw a border frame
static void draw_border(uint8_t *fb, int fb_width, int x, int y, int w, int h) {
    // Top and bottom
    draw_rect(fb, fb_width, x, y, w, 1, COLOR_BORDER);
    draw_rect(fb, fb_width, x, y + h - 1, w, 1, COLOR_BORDER);
    // Left and right
    draw_rect(fb, fb_width, x, y, 1, h, COLOR_BORDER);
    draw_rect(fb, fb_width, x + w - 1, y, 1, h, COLOR_BORDER);
}

// Draw a scrollbar on the right side
// x, y: position of scrollbar area
// h: height of scrollbar area
// total_items: total number of items
// visible_items: number of visible items
// scroll_pos: current scroll position (first visible item index)
static void draw_scrollbar(uint8_t *fb, int fb_width, int x, int y, int h, int total_items, int visible_items, int scroll_pos) {
    if (total_items <= visible_items) {
        return;  // No scrollbar needed
    }
    
    // Draw scrollbar track (dim)
    draw_rect(fb, fb_width, x, y, 4, h, COLOR_BG);
    draw_rect(fb, fb_width, x, y, 1, h, 8);  // Dim gray track
    
    // Calculate thumb position and size
    int thumb_h = (h * visible_items) / total_items;
    if (thumb_h < 8) thumb_h = 8;  // Minimum thumb size
    
    int max_scroll = total_items - visible_items;
    int thumb_y = y + ((h - thumb_h) * scroll_pos) / max_scroll;
    
    // Draw thumb (bright)
    draw_rect(fb, fb_width, x, thumb_y, 4, thumb_h, COLOR_BORDER);
}

void disk_ui_init(void) {
    ui_state = DISK_UI_HIDDEN;
    selected_drive = 0;
    selected_file = 0;
    selected_action = 0;
    scroll_offset = 0;
}

void disk_ui_init_with_emulator(mii_t *mii, int disk2_slot) {
    disk_ui_init();
    g_mii = mii;
    g_disk2_slot = disk2_slot;
    MII_DEBUG_PRINTF("Disk UI initialized with mii=%p, slot=%d\n", mii, disk2_slot);
}

extern uint8_t vram[2 * RAM_PAGES_PER_POOL * RAM_PAGE_SIZE];
extern FIL fp;
extern char selected_dir[128];

void disk_ui_show(void) {
    mutex_enter_blocking(&video_mutex);
    if (ui_state == DISK_UI_HIDDEN) {
        // Capture before the menu borrows Apple RAM for its disk-image list.
        basic_ready = remote_control_basic_prompt();
        home_message = NULL;
        FRANK_LED_PUT(true);
        FRESULT fr = f_open(&fp, "/tmp/apple.snap", FA_CREATE_ALWAYS | FA_WRITE);
        UINT wb = 0;
        if (fr == FR_OK) {
            fr = f_write(&fp, vram, sizeof(vram), &wb);
            FRESULT close_fr = f_close(&fp);
            if (close_fr != FR_OK) fr = close_fr;
        }
        FRANK_LED_PUT(false);
        if (fr != FR_OK || wb != sizeof(vram)) {
            printf("Disk UI: snapshot save failed; menu remains closed\n");
            mutex_exit(&video_mutex);
            return;
        }
        fb_needs_clear = true;

        FRANK_LED_PUT(true);
        // Scan for disk images
        int count = disk_scan_directory(selected_dir);
        if (count < 0) {
            count = -count;
            strcpy(selected_dir, "/");
        }
        printf("Found %d disk images\n", count);
        FRANK_LED_PUT(false);

        ui_state = DISK_UI_HOME;
        home_item = 0;
        selected_drive = 0;
        ui_dirty = true;
        ui_rendered = false;
        MII_DEBUG_PRINTF("Disk UI: showing drive selection\n");
    }
    mutex_exit(&video_mutex);
}

void disk_ui_hide(void) {
    mutex_enter_blocking(&video_mutex);
    FRANK_LED_PUT(true);
    FRESULT fr = f_open(&fp, "/tmp/apple.snap", FA_READ);
    UINT rb = 0;
    if (fr == FR_OK) {
        fr = f_read(&fp, vram, sizeof(vram), &rb);
        FRESULT close_fr = f_close(&fp);
        if (close_fr != FR_OK) fr = close_fr;
    }
    FRANK_LED_PUT(false);
    if (fr != FR_OK || rb != sizeof(vram)) {
        printf("Disk UI: snapshot restore failed; emulator remains paused\n");
        mutex_exit(&video_mutex);
        return;
    }

    ui_state = DISK_UI_HIDDEN;
    ui_rendered = false;
    ui_dirty = false;
    g_last_framebuffer = NULL;
    MII_DEBUG_PRINTF("Disk UI: hidden\n");
    mutex_exit(&video_mutex);
}

void disk_ui_toggle(void) {
    if (ui_state == DISK_UI_HIDDEN) {
        disk_ui_show();
    } else {
        disk_ui_hide();
    }
}

bool __scratch_x() disk_ui_is_visible(void) {
    __dmb();
    return ui_state != DISK_UI_HIDDEN;
}

bool disk_ui_needs_redraw(void) {
    return ui_dirty || !ui_rendered;
}

int disk_ui_get_selected_drive(void) {
    return selected_drive;
}

// Show the loading screen
void disk_ui_show_loading(void) {
    ui_state = DISK_UI_LOADING;
    ui_dirty = true;
    ui_rendered = false;
}

static bool read_only = false;  // toggle the Read-only item (or SPACE); writes go to the .bdsk copy, never the original image
static bool bdsk_exists = false;
static bool bdsk_recreate = false;
#define has_parent_dir  (strcmp(selected_dir, "/") != 0)

size_t disk_ui_describe(char *out, size_t cap) {
    if (!cap) return 0;
    size_t used = 0;
    out[0] = 0;
#define MENU_TEXT(...) do { \
    if (used < cap - 1) { \
        int written = snprintf(out + used, cap - used, __VA_ARGS__); \
        if (written > 0) used += (size_t)written < cap - used ? (size_t)written : cap - used - 1; \
    } \
} while (0)
    if (ui_state == DISK_UI_HOME) {
        MENU_TEXT("APPLE II\n\n");
        for (int i = 0; i < HOME_ITEMS; ++i)
            MENU_TEXT("%c %s\n", i == home_item ? '>' : ' ', home_label(i));
        MENU_TEXT("\n%s\n", home_message ? home_message : "Saved programs: DOS 3.3 / drive 1");
        MENU_TEXT("UP/DOWN: choose  B/C/RETURN: select\nA/ESC: resume  HOME: resume\n");
#if NETCARD_WEB_CONTROL
        char address[48]; web_control_address(address, sizeof(address));
        MENU_TEXT("\n%s\nSPACE also toggles web control.\n", address);
#endif
    } else if (ui_state == DISK_UI_PROGRAMS) {
        MENU_TEXT("SAVED PROGRAMS - DRIVE 1\n%.39s\n\n", g_loaded_disks[0].filename);
        if (program_count < 0) MENU_TEXT("Use a DOS 3.3 disk in drive 1.\nThis catalog could not be read.\n");
        else if (!program_count) MENU_TEXT("No Applesoft programs on this disk.\nCreate one in BASIC: SAVE MY PROGRAM\n");
        for (int i = program_scroll; i < program_count && i < program_scroll + PROGRAM_VISIBLE; ++i)
            MENU_TEXT("%c %s\n", i == program_item ? '>' : ' ', programs[i].name);
        MENU_TEXT("\nB/C/RETURN: options  A/ESC: back\nReopen this list to refresh (or SPACE).\n");
    } else if (ui_state == DISK_UI_PROGRAM_ACTION) {
        MENU_TEXT("SAVED PROGRAM\n%s\n\n", programs[program_item].name);
        MENU_TEXT("Replaces the program in memory.\nSave your current work first.\n\n");
        if (!basic_ready) MENU_TEXT("Return to the empty ] BASIC prompt\nbefore loading a saved program.\n\n");
        const char *actions[] = {"Run program", "Load without running", "Back"};
        for (int i = 0; i < 3; ++i) MENU_TEXT("%c %s\n", i == program_action ? '>' : ' ', actions[i]);
        MENU_TEXT("\nB/C/RETURN: select  A/ESC: back\n");
    } else if (ui_state == DISK_UI_SELECT_DRIVE) {
        MENU_TEXT("CHOOSE A DISK DRIVE\n\n");
        for (int i = 0; i < 2; ++i)
            MENU_TEXT("%c DRIVE %d\n  %.36s\n\n", i == selected_drive ? '>' : ' ', i + 1,
                g_loaded_disks[i].loaded ? g_loaded_disks[i].filename : "(empty)");
        MENU_TEXT("UP/DOWN: choose   B/C/RETURN: open\nA/ESCAPE: back to launcher\n");
#if NETCARD_WEB_CONTROL
        MENU_TEXT("\nSPACE toggles web control here.\n");
#endif
    } else if (ui_state == DISK_UI_SELECT_FILE) {
        MENU_TEXT("CHOOSE A DISK FOR DRIVE %d\n%.39s\n\n", selected_drive + 1, selected_dir);
        int base = has_parent_dir ? 1 : 0;
        int total = g_disk_count + base;
        for (int i = scroll_offset; i < total && i < scroll_offset + 16; ++i) {
            const char *name = base && i == 0 ? ".. (parent folder)" : g_disk_list[i - base].filename;
            MENU_TEXT("%c %.37s\n", i == selected_file ? '>' : ' ', name);
        }
        MENU_TEXT("\nUP/DOWN: choose   B/C/RETURN: select\nA/ESCAPE: back\n");
    } else if (ui_state == DISK_UI_SELECT_ACTION) {
        int index = selected_file - (has_parent_dir ? 1 : 0);
        MENU_TEXT("DISK ACTION\n%.39s\n\n", index >= 0 && index < g_disk_count ? g_disk_list[index].filename : "(no disk)");
        const char *actions[] = {"Boot - start this disk", "Insert - keep current program",
            read_only ? "Read-only: ON" : "Read-only: OFF", "Cancel"};
        for (int i = 0; i < DISK_ACTIONS; ++i) MENU_TEXT("%c %s\n", i == selected_action ? '>' : ' ', actions[i]);
        MENU_TEXT("\nBoot replaces the program in memory.\nUP/DOWN: choose   B/C/RETURN: do action\nA/ESCAPE: back   SPACE: read-only\n");
    } else if (ui_state == DISK_UI_LOADING) {
        MENU_TEXT("LOADING DISK...\nPlease wait.\n");
    }
#undef MENU_TEXT
    return used;
}

static bool disk_ui_delete_selected_file(void)
{
    if (ui_state != DISK_UI_SELECT_FILE)
        return false;
    int base = has_parent_dir ? 1 : 0;
    if (base && selected_file == 0)
        return false; // ".."
    int idx = selected_file - base;
    if (idx < 0 || idx >= g_disk_count)
        return false;
    disk_entry_t *e = &g_disk_list[idx];
    if (e->type == DIR_TYPE)
        return false; // директории не удаляем
    /* --- сохранить текущую позицию --- */
    int old_selected = selected_file;
    int old_scroll   = scroll_offset;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", selected_dir, e->filename);
    FRESULT fr = f_unlink(path);
    if (fr != FR_OK) {
        printf("Delete failed: %s (%d)\n", path, fr);
        return false;
    }
    /* --- пересканировать --- */
    int count = disk_scan_directory(selected_dir);
    if (count < 0)
        count = -count;

    int total = count + (has_parent_dir ? 1 : 0);

    /* --- восстановить selected_file --- */
    if (total == 0) {
        selected_file = 0;
        scroll_offset = 0;
    } else {
        if (old_selected >= total)
            selected_file = total - 1;
        else
            selected_file = old_selected;

        /* --- восстановить scroll_offset так, чтобы выделение было видно --- */
        scroll_offset = old_scroll;

        if (selected_file < scroll_offset)
            scroll_offset = selected_file;
        else if (selected_file >= scroll_offset + MAX_VISIBLE)
            scroll_offset = selected_file - MAX_VISIBLE + 1;

        if (scroll_offset < 0)
            scroll_offset = 0;
    }

    ui_dirty = true;
    return true;
}

// Handle loading complete - mount disk and perform action
static bool handle_disk_loaded(void) {
    disk_ui_hide();
    if (disk_ui_is_visible()) return false;
    if (g_mii) {
        int preserve_state = (selected_action == DISK_INSERT) ? 1 : 0;
        if (0 == disk_mount_to_emulator(
                selected_drive,
                g_mii,
                g_disk2_slot,
                preserve_state,
                read_only,
                bdsk_recreate
            )
        ) {
            printf("Disk UI: disk mounted successfully\n");
            
            if (selected_action == DISK_BOOT) {
                // Remember the startup disk, not an application's later
                // program/data-disk swap, which may not be bootable.
                disk_autoboot_save(selected_drive);
                printf("Disk UI: resetting CPU for disk boot\n");
                mii_reset(g_mii, true);
                
                mii_bank_t *sw_bank = &g_mii->bank[MII_BANK_SW];
                mii_bank_poke(sw_bank, SWKBD, 0);
                mii_bank_poke(sw_bank, SWAKD, 0);
                clear_held_key();
                
                uint8_t sw_byte = 0;
                mii_mem_access(g_mii, SWINTCXROMOFF, &sw_byte, true, true);
                printf("Disk UI: CPU reset complete\n");
            } else {  // INSERT
                printf("Disk UI: disk inserted (no reset)\n");
                
                mii_bank_t *sw_bank = &g_mii->bank[MII_BANK_SW];
                mii_bank_poke(sw_bank, SWKBD, 0);
                mii_bank_poke(sw_bank, SWAKD, 0);
                mii_bank_poke(sw_bank, 0xc061, 0);
                mii_bank_poke(sw_bank, 0xc062, 0);
                mii_bank_poke(sw_bank, 0xc063, 0);
                clear_held_key();
            }
        } else {
            MII_DEBUG_PRINTF("Disk UI: failed to mount disk to emulator\n");
            return false;
        }
    } else {
        MII_DEBUG_PRINTF("Disk UI: warning - no emulator reference, disk not mounted\n");
        return false;
    }
    return true;
}

bool disk_ui_mount_file(const char *name, int drive, bool boot) {
    if(!g_mii || drive<0 || drive>1 || (boot && drive!=0)) return false;
    // show saves Apple RAM before reusing it for the file list.
    disk_ui_show();
    if(!disk_ui_is_visible()) return false;
    mutex_enter_blocking(&video_mutex);
    strcpy(selected_dir,"/apple");
    int count=disk_scan_directory(selected_dir), index=-1;
    if(count>=0) for(int i=0;i<count;i++)
        if(g_disk_list[i].type!=DIR_TYPE && !strcmp(g_disk_list[i].filename,name)) { index=i; break; }
    selected_drive=drive; selected_action=boot?DISK_BOOT:DISK_INSERT;
    read_only=false; bdsk_recreate=false;
    ui_state=DISK_UI_LOADING; ui_dirty=true;
    mutex_exit(&video_mutex);
    if(index<0) { disk_ui_hide(); return false; }
    if(g_loaded_disks[drive].loaded && disk_eject_from_emulator(drive,g_mii,g_disk2_slot)<0) {
        disk_ui_hide(); return false;
    }
    if(disk_load_image(drive,index,true)<0) { disk_ui_hide(); return false; }
    return handle_disk_loaded();
}

static bool disk_ui_select_loaded_file(int drive)
{
    if (!g_loaded_disks[drive].loaded)
        return false;

    const char *name = g_loaded_disks[drive].filename;
    int base = has_parent_dir ? 1 : 0;

    for (int i = 0; i < g_disk_count; i++) {
        if (strcmp(g_disk_list[i].filename, name) == 0) {
            selected_file = i + base;

            /* сделать элемент видимым */
            if (selected_file < scroll_offset)
                scroll_offset = selected_file;
            else if (selected_file >= scroll_offset + MAX_VISIBLE)
                scroll_offset = selected_file - MAX_VISIBLE + 1;

            if (scroll_offset < 0)
                scroll_offset = 0;

            return true;
        }
    }

    return false; // файл не найден
}

static void refresh_programs(void) {
    program_count = disk_saved_programs(g_mii, programs, DOS_PROGRAM_MAX);
    program_item = program_scroll = 0;
    ui_state = DISK_UI_PROGRAMS;
    ui_dirty = true;
}

static void launch_command(const char *command) {
    disk_ui_hide();
    if (disk_ui_is_visible()) return; // snapshot restore must succeed first
    clear_held_key();
    if (!typing_try_literal((const uint8_t *)command, strlen(command))) {
        disk_ui_show();
        home_message = "Typing busy. Resume and try again.";
    }
}

static bool handle_launcher_key(uint8_t key) {
    int direction = key == 0x0b || key == 0x08 ? -1 :
                    key == 0x0a || key == 0x15 ? 1 : 0;
    ui_dirty = true;
    if (ui_state == DISK_UI_HOME) {
        if (direction) { home_item = (home_item + direction + HOME_ITEMS) % HOME_ITEMS; home_message = NULL; }
        if (key == 0x1b) disk_ui_hide();
#if NETCARD_WEB_CONTROL
        if (key == ' ') web_control_toggle();
#endif
        if (key == '\r') {
            switch (home_item) {
                case 0: disk_ui_hide(); break;
                case 1: refresh_programs(); break;
                case 2:
                    if (basic_ready) launch_command("RUN\r");
                    else home_message = "Run needs an empty ] BASIC prompt.";
                    break;
                case 3:
                    disk_ui_hide();
                    if (!disk_ui_is_visible()) { clear_held_key(); remote_control_key(3); }
                    break;
                case 4: ui_state = DISK_UI_SELECT_DRIVE; break;
#if NETCARD_WEB_CONTROL
                case 5: web_control_toggle(); break;
#endif
            }
        }
        // Keep the existing keyboard shortcuts to each disk drive.
        if (key == '1' || key == '2') {
            ui_state = DISK_UI_SELECT_DRIVE;
            return disk_ui_handle_key(key);
        }
    } else if (ui_state == DISK_UI_PROGRAMS) {
        if (key == 0x1b) ui_state = DISK_UI_HOME;
        if (key == ' ') refresh_programs();
        if (direction && program_count > 0) {
            program_item = (program_item + direction + program_count) % program_count;
            if (program_item < program_scroll) program_scroll = program_item;
            if (program_item >= program_scroll + PROGRAM_VISIBLE)
                program_scroll = program_item - PROGRAM_VISIBLE + 1;
        }
        if (key == '\r' && program_count > 0) {
            program_action = basic_ready ? 0 : 2;
            ui_state = DISK_UI_PROGRAM_ACTION;
        }
    } else {
        if (direction) program_action = (program_action + direction + 3) % 3;
        if (key == 0x1b || (key == '\r' && program_action == 2)) ui_state = DISK_UI_PROGRAMS;
        else if (key == '\r' && basic_ready) {
            char command[64];
            if (dos_program_command(command, sizeof(command), programs[program_item].name,
                                    program_action == 0)) launch_command(command);
        }
    }
    return true;
}

bool disk_ui_handle_key(uint8_t key) {
    if (ui_state == DISK_UI_HIDDEN || ui_state == DISK_UI_LOADING) {
        return false;
    }
    if (ui_state == DISK_UI_HOME || ui_state == DISK_UI_PROGRAMS ||
        ui_state == DISK_UI_PROGRAM_ACTION) return handle_launcher_key(key);
    
    MII_DEBUG_PRINTF("Disk UI key: 0x%02X in state %d\n", key, ui_state);
    
    bool handled = false;
    int total_items = g_disk_count + (has_parent_dir ? 1 : 0);
    
    switch (key) {
        case 0x1B:  // Escape
            if (ui_state == DISK_UI_SELECT_FILE) {
                ui_state = DISK_UI_SELECT_DRIVE;
                ui_dirty = true;
            } else if (ui_state == DISK_UI_SELECT_ACTION) {
                ui_state = DISK_UI_SELECT_FILE;
                ui_dirty = true;
            } else if (ui_state == DISK_UI_SELECT_DRIVE) {
                ui_state = DISK_UI_HOME;
                ui_dirty = true;
            } else {
                disk_ui_hide();
            }
            handled = true;
            break;
            
        case 0x0D:  // Enter
            if (ui_state == DISK_UI_SELECT_DRIVE) {
                // Proceed to file selection
                ui_state = DISK_UI_SELECT_FILE;
                /* попытаться перейти к уже загруженному диску */
                if (!disk_ui_select_loaded_file(selected_drive)) {
                    selected_file = 0;
                    scroll_offset = 0;
                }
                ui_dirty = true;
                MII_DEBUG_PRINTF("Disk UI: selecting file for drive %d\n", selected_drive + 1);
                handled = true;
                break;
            }
            if (ui_state == DISK_UI_SELECT_FILE) {

                // --- virtual ".." ---
                if (has_parent_dir && selected_file == 0) {
                    // go to parent directory
                    char *slash = strrchr(selected_dir, '/');
                    if (slash && slash != selected_dir) {
                        *slash = '\0';
                    } else {
                        // "/apple" -> "/"
                        strcpy(selected_dir, "/");
                    }

                    int count = disk_scan_directory(selected_dir);
                    if (count < 0) {
                        count = -count;
                        strcpy(selected_dir, "/");
                    }
                    selected_file = 0;
                    scroll_offset = 0;
                    ui_dirty = true;
                    handled = true;
                    break;
                }
                // --- real entry ---
                int base = has_parent_dir ? 1 : 0;
                int idx = selected_file - base;
                if (idx >= 0 && idx < g_disk_count) {
                    if (g_disk_list[idx].type == DIR_TYPE) {
                        // enter directory
                        if (strcmp(selected_dir, "/") == 0) {
                            snprintf(selected_dir, sizeof(selected_dir),
                                    "/%s", g_disk_list[idx].filename);
                        } else {
                            size_t len = strlen(selected_dir);
                            snprintf(selected_dir + len,
                                    sizeof(selected_dir) - len,
                                    "/%s", g_disk_list[idx].filename);
                        }

                        int count = disk_scan_directory(selected_dir);
                        if (count < 0) {
                            count = -count;
                            strcpy(selected_dir, "/");
                        }

                        selected_file = 0;
                        scroll_offset = 0;
                        ui_dirty = true;
                    } else {
                        // file -> action menu
                        ui_state = DISK_UI_SELECT_ACTION;
                        /// read current prefereces for this drive, may be overriden later
                        // an empty drive slot must not read as "read-only"
                        read_only = g_loaded_disks[selected_drive].loaded ? !g_loaded_disks[selected_drive].write_back : false;
                        selected_action = 0;
                        ui_dirty = true;
                        MII_DEBUG_PRINTF("Disk UI: selecting action for file %d\n", selected_file);
                        int base = has_parent_dir ? 1 : 0;
                        disk_entry_t *entry = &g_disk_list[selected_file - base];
                        bdsk_exists = disk_bdsk_exists2( entry->filename );
                        if (!bdsk_exists) bdsk_recreate = false;
                    }
                }
                handled = true;
                break;
                // Proceed to action selection
            }
            if (ui_state == DISK_UI_SELECT_ACTION) {
                if (selected_action == DISK_CANCEL) {
                    ui_state = DISK_UI_SELECT_FILE;
                    ui_dirty = true;
                } else if (selected_action == DISK_READ_ONLY) {
                    read_only = !read_only;
                    ui_dirty = true;
                } else {
                    // Boot or Insert - show loading screen and load disk
                    MII_DEBUG_PRINTF("Disk UI: loading disk %d to drive %d (%s)\n", 
                           selected_file, selected_drive + 1,
                           selected_action == 0 ? "BOOT" : "INSERT");
                    
                    disk_ui_show_loading();
                    int base = has_parent_dir ? 1 : 0;
                    if (g_loaded_disks[selected_drive].loaded && g_mii) {
                        // flush and clear the old disk under its own name before the slot is reused
                        if (disk_eject_from_emulator(selected_drive, g_mii, g_disk2_slot) < 0) {
                            ui_state = DISK_UI_SELECT_FILE;
                            ui_dirty = true;
                            break;
                        }
                    }
                    if (disk_load_image(selected_drive, selected_file - base, !read_only) == 0) {
                        handle_disk_loaded();
                    } else {
                        // Failed to load - go back to file selection
                        ui_state = DISK_UI_SELECT_FILE;
                        ui_dirty = true;
                    }
                }
            }
            handled = true;
            break;
            
        case 0xFD:  // Page Up
            if (ui_state == DISK_UI_SELECT_FILE && total_items > 0) {
                int step = MAX_VISIBLE / 2;
                if (selected_file > 0) {
                    selected_file = (selected_file > step) ? (selected_file - step) : 0;

                    if (selected_file < scroll_offset) {
                        scroll_offset = selected_file;
                    }
                    ui_dirty = true;
                }
                handled = true;
            }
            break;
        case 0xFE:  // Page Down
            if (ui_state == DISK_UI_SELECT_FILE && total_items > 0) {
                int step = MAX_VISIBLE / 2;
                if (selected_file < total_items - 1) {
                    int max_idx = total_items - 1;
                    selected_file = (selected_file + step < max_idx) ? (selected_file + step) : max_idx;

                    if (selected_file >= scroll_offset + MAX_VISIBLE) {
                        scroll_offset = selected_file - MAX_VISIBLE + 1;
                    }
                    ui_dirty = true;
                }
                handled = true;
            }
            break;

        case 0x08:  // Left arrow / backspace
        case 0x0B:  // Up arrow
            if (ui_state == DISK_UI_SELECT_DRIVE) {
                // Wrap around: 0 -> 1, 1 -> 0
                selected_drive = 1 - selected_drive;
                ui_dirty = true;
            } else if (ui_state == DISK_UI_SELECT_FILE) {
                if (total_items > 0) {
                    if (selected_file > 0) {
                        selected_file--;
                    } else {
                        // Wrap to last item
                        selected_file = total_items - 1;
                        scroll_offset = (total_items > MAX_VISIBLE) ? total_items - MAX_VISIBLE : 0;
                    }
                    if (selected_file < scroll_offset) {
                        scroll_offset = selected_file;
                    }
                    ui_dirty = true;
                }
            } else if (ui_state == DISK_UI_SELECT_ACTION) {
                if (selected_action > 0) {
                    selected_action--;
                } else {
                    selected_action = DISK_ACTIONS - 1;
                }
                ui_dirty = true;
            }
            handled = true;
            break;
            
        case 0x15:  // Right arrow
        case 0x0A:  // Down arrow
            if (ui_state == DISK_UI_SELECT_DRIVE) {
                // Wrap around: 0 -> 1, 1 -> 0
                selected_drive = 1 - selected_drive;
                ui_dirty = true;
            } else if (ui_state == DISK_UI_SELECT_FILE) {
                if (total_items > 0) {
                    if (selected_file < total_items - 1) {
                        selected_file++;
                    } else {
                        // Wrap to first item
                        selected_file = 0;
                        scroll_offset = 0;
                    }
                    if (selected_file >= scroll_offset + MAX_VISIBLE) {
                        scroll_offset = selected_file - MAX_VISIBLE + 1;
                    }
                    ui_dirty = true;
                }
            } else if (ui_state == DISK_UI_SELECT_ACTION) {
                if (selected_action < DISK_ACTIONS - 1) {
                    selected_action++;
                } else {
                    selected_action = 0;  // Wrap to Boot
                }
                ui_dirty = true;
            }
            handled = true;
            break;

        case ' ':  // Space = toggle Read-Only
#if NETCARD_WEB_CONTROL
            if (ui_state == DISK_UI_SELECT_DRIVE) {
                web_control_toggle();
                ui_dirty = true;
                handled = true;
            }
#endif
            if (ui_state == DISK_UI_SELECT_ACTION) {
                read_only = !read_only;
                ui_dirty = true;
                handled = true;
            }
            break;

        case 'D':  // D = toggle bdsk_recreate
            if (ui_state == DISK_UI_SELECT_DRIVE) {
                int drive = selected_drive;
                if (g_loaded_disks[drive].loaded) {
                    // eject from emulator (flush + clear floppy)
                    if (disk_eject_from_emulator(drive, g_mii, g_disk2_slot) < 0) break;
                    // clear loader state
                    disk_unload_image(drive);
                    ui_dirty = true;
                }
                handled = true;
                break;
            }
            if (ui_state == DISK_UI_SELECT_ACTION) {
                bdsk_recreate = !bdsk_recreate;
                ui_dirty = true;
                handled = true;
                break;
            }
            if (ui_state == DISK_UI_SELECT_FILE) {
                handled = disk_ui_delete_selected_file();
                break;
            }
            break;
        case '1':
            if (ui_state == DISK_UI_SELECT_DRIVE) {
                selected_drive = 0;
                ui_state = DISK_UI_SELECT_FILE;
                selected_file = 0;
                scroll_offset = 0;
                ui_dirty = true;
            }
            handled = true;
            break;
            
        case '2':
            if (ui_state == DISK_UI_SELECT_DRIVE) {
                selected_drive = 1;
                ui_state = DISK_UI_SELECT_FILE;
                selected_file = 0;
                scroll_offset = 0;
                ui_dirty = true;
            }
            handled = true;
            break;
    }
    
    return handled;
}

void disk_ui_render(uint8_t *framebuffer, int width, int height) {
    disk_ui_state_t state = ui_state;
    
    if (state == DISK_UI_HIDDEN) {
        return;
    }

    if (fb_needs_clear) {
        memset(framebuffer, 0x00, (width * height) >> 1); // 4 bpp
        fb_needs_clear = false;
    }

    if (framebuffer != g_last_framebuffer) {
        ui_dirty = true;
        ui_rendered = false;
        g_last_framebuffer = framebuffer;
    }
    
    if (!ui_dirty && ui_rendered) {
        return;
    }
    
    (void)height;
    
    int drive = selected_drive;
    int sel_file = selected_file;
    int sel_action = selected_action;
    int scroll = scroll_offset;
    
    int content_x = UI_X + UI_PADDING;
    int content_y = UI_Y + HEADER_HEIGHT + UI_PADDING;
    int content_width = UI_WIDTH - UI_PADDING * 2;
    int max_chars = (content_width - 4) / CHAR_WIDTH;
    
    // Always do full redraw for simplicity
    // Draw dialog background
    draw_rect(framebuffer, width, UI_X, UI_Y, UI_WIDTH, UI_HEIGHT, COLOR_BG);
    
    // Draw border
    draw_border(framebuffer, width, UI_X, UI_Y, UI_WIDTH, UI_HEIGHT);
    
    if (state == DISK_UI_HOME) {
        draw_header(framebuffer, width, UI_X, UI_Y, UI_WIDTH, " Apple II ");
        int y = content_y + 4;
        for (int i = 0; i < HOME_ITEMS; ++i, y += 16)
            draw_menu_item(framebuffer, width, content_x, y, content_width,
                           home_label(i), max_chars, home_item == i);
        y += 6;
        draw_string_truncated(framebuffer, width, content_x, y,
            home_message ? home_message : "Saved programs: DOS 3.3 / drive 1", max_chars, COLOR_TEXT);
#if NETCARD_WEB_CONTROL
        y += 20;
        char address[48]; web_control_address(address, sizeof(address));
        draw_string_truncated(framebuffer, width, content_x, y, address, max_chars, COLOR_TEXT);
#endif
        draw_string(framebuffer, width, content_x, UI_Y + UI_HEIGHT - 16,
            "HOME / A / Esc: resume Apple II", COLOR_TEXT);
    } else if (state == DISK_UI_PROGRAMS) {
        draw_header(framebuffer, width, UI_X, UI_Y, UI_WIDTH, " Saved programs - Drive 1 ");
        int y = content_y;
        draw_string_truncated(framebuffer, width, content_x, y,
            g_loaded_disks[0].loaded ? g_loaded_disks[0].filename : "No disk in drive 1", max_chars, COLOR_TEXT);
        y += 20;
        if (program_count < 0) {
            draw_string(framebuffer, width, content_x, y, "Use a DOS 3.3 disk in drive 1.", COLOR_TEXT);
            draw_string(framebuffer, width, content_x, y+14, "This catalog could not be read.", COLOR_TEXT);
            draw_string(framebuffer, width, content_x, y+40, "A: back, then Choose a disk.", COLOR_TEXT);
        } else if (!program_count) {
            draw_string(framebuffer, width, content_x, y, "No Applesoft programs on this disk.", COLOR_TEXT);
            draw_string(framebuffer, width, content_x, y+24, "Create one in BASIC, then:", COLOR_TEXT);
            draw_string(framebuffer, width, content_x, y+38, "SAVE MY PROGRAM", COLOR_TEXT);
        } else {
            for (int i = program_scroll; i < program_count && i < program_scroll + PROGRAM_VISIBLE; ++i, y += LINE_HEIGHT)
                draw_menu_item(framebuffer, width, content_x, y, content_width-8,
                    programs[i].name, max_chars-2, program_item == i);
            if (program_count > PROGRAM_VISIBLE)
                draw_scrollbar(framebuffer, width, UI_X+UI_WIDTH-UI_PADDING-4,
                    content_y+20, PROGRAM_VISIBLE*LINE_HEIGHT, program_count, PROGRAM_VISIBLE, program_scroll);
        }
        draw_string(framebuffer, width, content_x, UI_Y+UI_HEIGHT-16,
            "Reopen list to refresh (or Space)", COLOR_TEXT);
    } else if (state == DISK_UI_PROGRAM_ACTION) {
        draw_header(framebuffer, width, UI_X, UI_Y, UI_WIDTH, " Saved program ");
        int y = content_y + 4;
        draw_string(framebuffer, width, content_x, y, programs[program_item].name, COLOR_TEXT);
        draw_string(framebuffer, width, content_x, y+22, "Replaces the program in memory.", COLOR_TEXT);
        draw_string(framebuffer, width, content_x, y+36, "Save your current work first.", COLOR_TEXT);
        if (!basic_ready) {
            draw_string(framebuffer, width, content_x, y+60, "Return to the empty ] BASIC prompt", COLOR_TEXT);
            draw_string(framebuffer, width, content_x, y+74, "before loading a saved program.", COLOR_TEXT);
        }
        const char *actions[] = {"Run program", "Load without running", "Back"};
        for (int i = 0; i < 3; ++i)
            draw_menu_item(framebuffer, width, content_x, y+102+i*16, content_width,
                actions[i], max_chars, program_action == i);
    } else if (state == DISK_UI_LOADING) {
        // Loading screen
        draw_header(framebuffer, width, UI_X, UI_Y, UI_WIDTH, " Loading... ");
        
        int msg_y = UI_Y + UI_HEIGHT / 2 - CHAR_HEIGHT / 2;
        draw_string(framebuffer, width, content_x + 80, msg_y, "Please wait...", COLOR_TEXT);
        
    } else if (state == DISK_UI_SELECT_DRIVE) {
        // Drive selection
        draw_header(framebuffer, width, UI_X, UI_Y, UI_WIDTH, " Select Drive ");
        
        int y = content_y + 8;
        
        // Drive 1
        char drive1_text[64];
        if (g_loaded_disks[0].loaded) {
            snprintf(drive1_text, sizeof(drive1_text), "Drive 1: %.32s", g_loaded_disks[0].filename);
        } else {
            strcpy(drive1_text, "Drive 1: (empty)");
        }
        draw_menu_item(framebuffer, width, content_x, y, content_width, drive1_text, max_chars, drive == 0);
        y += LINE_HEIGHT + 2;
        
        // Drive 2
        char drive2_text[64];
        if (g_loaded_disks[1].loaded) {
            snprintf(drive2_text, sizeof(drive2_text), "Drive 2: %.32s", g_loaded_disks[1].filename);
        } else {
            strcpy(drive2_text, "Drive 2: (empty)");
        }
        draw_menu_item(framebuffer, width, content_x, y, content_width, drive2_text, max_chars, drive == 1);
#if NETCARD_WEB_CONTROL
        y += 30;
        draw_string(framebuffer, width, content_x, y,
            web_control_enabled() ? "Web control: ON" : "Web control: OFF", COLOR_TEXT);
        char address[48];
        web_control_address(address, sizeof(address));
        draw_string(framebuffer, width, content_x, y + 14, address, COLOR_TEXT);
        draw_string(framebuffer, width, content_x, y + 28, "Open on a phone or computer on this WiFi.", COLOR_TEXT);
        draw_string(framebuffer, width, content_x, y + 42, "Change Web control in the HOME menu.", COLOR_TEXT);
#endif
        
        // Instructions below dialog border - clear area first
        int footer_y = UI_Y + UI_HEIGHT + 4;
        draw_rect(framebuffer, width, UI_X, footer_y, UI_WIDTH, LINE_HEIGHT, COLOR_BG);
        draw_string(framebuffer, width, content_x, footer_y, "[1/2] Select  [Enter] OK  [Esc] Cancel [D]el", COLOR_TEXT);
        
    } else if (state == DISK_UI_SELECT_FILE) {
        // File selection
        char title[48];
        snprintf(title, sizeof(title), " Drive %d - Select Disk ", drive + 1);
        draw_header(framebuffer, width, UI_X, UI_Y, UI_WIDTH, title);
        int base = has_parent_dir ? 1 : 0;
        int total_items = g_disk_count + base;        
        int y = content_y;
        
        if (g_disk_count == 0) {
            draw_string(framebuffer, width, content_x, y, "No disk images found", COLOR_TEXT);
            draw_string(framebuffer, width, content_x, y + LINE_HEIGHT, "Place .bdsk/.dsk/.woz/.nib files there", COLOR_TEXT);
        } else {
            // Calculate visible range
            int visible = (total_items < MAX_VISIBLE) ? total_items : MAX_VISIBLE;
            int list_height = visible * LINE_HEIGHT;
            
            for (int i = 0; i < visible; i++) {
                int ui_idx = scroll + i;
                if (ui_idx >= total_items) break;

                bool is_selected = (ui_idx == sel_file);

                if (base && ui_idx == 0) {
                    // virtual ".."
                    draw_menu_item(
                        framebuffer,
                        width,
                        content_x,
                        y,
                        content_width - 8,
                        "..",
                        max_chars - 2,
                        is_selected
                    );
                } else {
                    int real_idx = ui_idx - base;
                    if (real_idx < 0 || real_idx >= g_disk_count) break;

                    draw_menu_item(
                        framebuffer,
                        width,
                        content_x,
                        y,
                        content_width - 8,
                        g_disk_list[real_idx].filename,
                        max_chars - 2,
                        is_selected
                    );
                }

                y += LINE_HEIGHT;
            }
            
            // Draw scrollbar if needed
            if (g_disk_count > MAX_VISIBLE) {
                int scrollbar_x = UI_X + UI_WIDTH - UI_PADDING - 4;
                draw_scrollbar(framebuffer, width, scrollbar_x, content_y, list_height, 
                              g_disk_count, visible, scroll);
            }
        }
        
        // Instructions below dialog border - clear area first
        int footer_y = UI_Y + UI_HEIGHT + 4;
        draw_rect(framebuffer, width, UI_X, footer_y, UI_WIDTH, LINE_HEIGHT, COLOR_BG);
        draw_string(framebuffer, width, content_x, footer_y, "[Up/Dn] Select  [Enter] OK  [Esc] Back", COLOR_TEXT);
        
    } else if (state == DISK_UI_SELECT_ACTION) {
        // Action selection
        char title[48];
        snprintf(title, sizeof(title), " Drive %d ", drive + 1);
        draw_header(framebuffer, width, UI_X, UI_Y, UI_WIDTH, title);
        
        int y = content_y + 4;
        
        // Show selected file
        char file_label[64];
        int base = has_parent_dir ? 1 : 0;
        snprintf(file_label, sizeof(file_label), "File: %.40s", g_disk_list[sel_file - base].filename);
        draw_string_truncated(framebuffer, width, content_x, y, file_label, max_chars, COLOR_TEXT);
        y += LINE_HEIGHT + 8;

        // Optional keyboard-only sidecar maintenance.
        char label[32];
        if (bdsk_exists) {
            snprintf(label, sizeof(label), "[%c] Recreate .bdsk [D]", bdsk_recreate ? 'x' : ' ');
            draw_string(framebuffer, width, content_x, y, label, COLOR_TEXT);
            y += LINE_HEIGHT + 4;
        }
        
        // Action options
        draw_string(framebuffer, width, content_x, y, "Select action:", COLOR_TEXT);
        y += LINE_HEIGHT + 4;
        
        draw_menu_item(framebuffer, width, content_x + 10, y, content_width - 20, 
                      "Boot   - Insert and reboot", max_chars - 4, sel_action == 0);
        y += LINE_HEIGHT + 2;
        
        draw_menu_item(framebuffer, width, content_x + 10, y, content_width - 20,
                      "Insert - Replace disk (no reboot)", max_chars - 4, sel_action == 1);
        y += LINE_HEIGHT + 2;
        
        draw_menu_item(framebuffer, width, content_x + 10, y, content_width - 20,
                      read_only ? "Read-only: ON" : "Read-only: OFF", max_chars - 4, sel_action == DISK_READ_ONLY);
        y += LINE_HEIGHT + 2;
        draw_menu_item(framebuffer, width, content_x + 10, y, content_width - 20,
                      "Cancel", max_chars - 4, sel_action == DISK_CANCEL);
        
        // Instructions below dialog border - clear area first
        int footer_y = UI_Y + UI_HEIGHT + 4;
        draw_rect(framebuffer, width, UI_X, footer_y, UI_WIDTH, LINE_HEIGHT, COLOR_BG);
        draw_string(framebuffer, width, content_x, footer_y, "[Up/Dn] Select  [Enter] OK  [Esc] Back", COLOR_TEXT);
    }

    // Match the physical left/back, middle/select, right/forward arrangement.
    int button_footer = UI_Y + UI_HEIGHT + 4;
    draw_rect(framebuffer, width, UI_X, button_footer, UI_WIDTH, LINE_HEIGHT, COLOR_BG);
    draw_string(framebuffer, width, content_x, button_footer,
#ifdef BOARD_TUFTY
        "Up/Down: move  A: back  B/C: select", COLOR_TEXT);
#else
        "Up/Down: choose  Enter: OK  Esc: back", COLOR_TEXT);
#endif
    
    ui_dirty = false;
    ui_rendered = true;
}
