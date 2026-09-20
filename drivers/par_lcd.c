/*
 * par_lcd.c - ST7789 8-bit parallel (8080) video driver (Pimoroni Tufty 2350)
 *
 * Same framebuffer contract as drivers/spi_lcd.c (and drivers/HDMI.c):
 * 320x240, 4 bits per pixel, packed two pixels per byte (38,400 bytes).
 * The LOW nibble of each byte is the LEFT pixel of the pair, the HIGH
 * nibble is the RIGHT pixel (see mii_video.c: even x goes to the low
 * nibble, odd x is ORed in shifted left by 4).
 *
 * Unlike HDMI/VGA there is no continuous scanout: graphics_present() pushes
 * one full frame to the panel on demand. It expands the 4-bit framebuffer
 * through the 16-entry palette into RGB565 one line at a time using two
 * 640-byte line buffers, converting line n+1 while DMA sends line n.
 *
 * The only difference from spi_lcd.c is the bus: an 8-bit 8080 parallel
 * interface driven by PIO2 + DMA instead of SPI. PIO/DMA setup, the panel
 * init sequence and the stall-wait logic are ported from Pimoroni's own
 * MicroPython driver for this badge (reference/st7789/: st7789.hpp is the
 * PIO/DMA setup, st7789.cpp is the init sequence, command(), DMA wait and
 * pio_sm_block_until_stalled).
 */

#include "../src/board_config.h"
#include "par_lcd.h"
#include "debug_log.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "par_lcd.pio.h"
#include "pico/stdlib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define LCD_LINE_BYTES (LCD_WIDTH * 2)

// ST7789 commands (same names as the reference st7789.cpp reg enum)
#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT 0x11
#define ST7789_INVON 0x21
#define ST7789_DISPON 0x29
#define ST7789_CASET 0x2A
#define ST7789_RASET 0x2B
#define ST7789_RAMWR 0x2C
#define ST7789_TEON 0x35
#define ST7789_MADCTL 0x36
#define ST7789_COLMOD 0x3A
#define ST7789_STE 0x44
#define ST7789_RAMCTRL 0xB0
#define ST7789_PORCTRL 0xB2
#define ST7789_GCTRL 0xB7
#define ST7789_VCOMS 0xBB
#define ST7789_LCMCTRL 0xC0
#define ST7789_VDVVRHEN 0xC2
#define ST7789_VRHS 0xC3
#define ST7789_VDVS 0xC4
#define ST7789_FRCTRL2 0xC6
#define ST7789_PWCTRL1 0xD0
#define ST7789_GMCTRP1 0xE0
#define ST7789_GMCTRN1 0xE1

// MADCTL for 320x240 landscape. spi_lcd.c uses 0x70 (MX|MV|ML) on the Display
// Pack; on the Tufty 2350 that came up upside down (verified 2026-09-20), so
// both mirror bits are flipped: MY|MV|ML = 0xB0 (Pimoroni's ROTATE_180).
// This byte is the orientation knob.
#define ST7789_MADCTL_LANDSCAPE 0xB0

// Bounded TE wait so a missing/unwired tearing-effect line never hangs.
#define LCD_TE_TIMEOUT_US 25000u

static uint8_t graphics_buffer[LCD_WIDTH * LCD_HEIGHT / 2] __aligned(4096) = { 0 };

static uint16_t palette_rgb565[16] = { 0 };
static uint16_t bgcolor_rgb565 = 0;

static uint32_t graphics_width = LCD_WIDTH;
static uint32_t graphics_height = LCD_HEIGHT;
static int graphics_shift_x = 0;
static int graphics_shift_y = 0;

static volatile uint32_t graphics_frame_count = 0;

static int dma_lcd = -1;
static uint lcd_sm = 0;
static float lcd_clkdiv = 0.0f;
static uint16_t linebuf[2][LCD_WIDTH];

// Scanline lock shared with mii_video.c. HDMI/VGA drivers set this while
// the scanout ISR reads a row so the renderer waits instead of tearing.
// There is no concurrent scanout here (graphics_present runs on core 1
// after video_core_iteration), so this stays -1 and the waits never fire.
volatile int lock_y = -1;

static uint16_t rgb888_to_rgb565(uint32_t color888) {
    uint8_t r = (color888 >> 16) & 0xff;
    uint8_t g = (color888 >> 8) & 0xff;
    uint8_t b = color888 & 0xff;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Reference st7789.cpp pio_sm_block_until_stalled: set and poll the TXSTALL
// bit in fdebug for this state machine.
static inline void pio_sm_block_until_stalled(PIO pio, uint sm) {
    uint32_t sm_stall_mask = 1u << (sm + PIO_FDEBUG_TXSTALL_LSB);
    pio->fdebug = sm_stall_mask;
    while (!(pio->fdebug & sm_stall_mask)) {
        tight_loop_contents();
    }
}

// Wait for the in-flight DMA transfer (if any) AND for the PIO TX FIFO to
// stall, i.e. every byte has left the bus. CS/DC must not move before both
// are done, or the last bytes go out under the wrong select/mode level.
static inline void lcd_wait_idle(void) {
    if (dma_lcd >= 0) {
        dma_channel_wait_for_finish_blocking((uint)dma_lcd);
    }
    pio_sm_block_until_stalled(LCD_PIO, lcd_sm);
}

// Bulk byte path: DMA channel, 8-bit transfers, read increment on, write to
// the PIO TX FIFO. An 8-bit bus write to the TX FIFO lands the byte in all
// four byte lanes and the state machine shifts out the top 8 bits, so each
// DMA byte is one bus byte.
static void lcd_write_blocking(const uint8_t *src, size_t len) {
    dma_channel_set_trans_count((uint)dma_lcd, (uint32_t)len, false);
    dma_channel_set_read_addr((uint)dma_lcd, src, true);
    lcd_wait_idle();
}

static void lcd_start_dma(const uint8_t *src, size_t len) {
    dma_channel_set_trans_count((uint)dma_lcd, (uint32_t)len, false);
    dma_channel_set_read_addr((uint)dma_lcd, src, true);
}

// Reconfigure the DMA read increment (reference configure_dma). Used to
// clock out a repeated zero byte when clearing the panel GRAM.
static void lcd_configure_dma(bool read_increment) {
    dma_channel_config cfg = dma_channel_get_default_config((uint)dma_lcd);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, read_increment);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(LCD_PIO, lcd_sm, true));
    dma_channel_configure((uint)dma_lcd, &cfg, &LCD_PIO->txf[lcd_sm], NULL, 0, false);
}

// Single bytes without DMA (commands): the state machine shifts out the top
// 8 bits, so left-align the byte in the 32-bit FIFO slot.
static void lcd_put_byte(uint8_t b) {
    pio_sm_put_blocking(LCD_PIO, lcd_sm, (uint32_t)b << 24);
}

static void lcd_cmd(uint8_t cmd) {
    lcd_wait_idle();
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    lcd_put_byte(cmd);
    lcd_wait_idle();
    gpio_put(LCD_PIN_CS, 1);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {
    lcd_wait_idle();
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    lcd_put_byte(cmd);
    lcd_wait_idle();
    if (len) {
        gpio_put(LCD_PIN_DC, 1);
        lcd_write_blocking(data, len);
    }
    gpio_put(LCD_PIN_CS, 1);
}

// Panel init: exactly the reference ST7789::init() sequence and values,
// except CASET/RASET/MADCTL (we write row-major landscape frames: CASET =
// 0..319, RASET = 0..239, MADCTL = MX|MV|ML) and the backlight (plain GPIO
// high here, not PWM).
static void lcd_init_panel(void) {
    static const uint8_t d_porctrl[] = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
    static const uint8_t d_lcmctrl[] = { 0x2C };
    static const uint8_t d_vdvvrhen[] = { 0x01 };
    static const uint8_t d_vrhs[] = { 0x0F };
    static const uint8_t d_vdvs[] = { 0x20 };
    static const uint8_t d_pwctrl1[] = { 0xA4, 0xA1 };
    static const uint8_t d_frctrl2[] = { 0x0F };
    static const uint8_t d_ramctrl[] = { 0x00, 0xC0 };
    static const uint8_t d_gctrl[] = { 0x35 };
    static const uint8_t d_vcoms[] = { 0x1B };
    static const uint8_t d_gmctrp1[] = { 0xF0, 0x00, 0x06, 0x04, 0x05, 0x05,
        0x31, 0x44, 0x48, 0x36, 0x12, 0x12, 0x2B, 0x34 };
    static const uint8_t d_gmctrn1[] = { 0xF0, 0x0B, 0x0F, 0x0F, 0x0D, 0x26,
        0x31, 0x43, 0x47, 0x38, 0x14, 0x14, 0x2C, 0x32 };
    static const uint8_t d_colmod[] = { 0x05 }; // RGB565
    static const uint8_t d_madctl[] = { ST7789_MADCTL_LANDSCAPE };
    static const uint8_t d_caset[] = { 0x00, 0x00, 0x01, 0x3F }; // 0..319
    static const uint8_t d_raset[] = { 0x00, 0x00, 0x00, 0xEF }; // 0..239
    static const uint8_t d_teon[] = { 0x00 };
    static const uint8_t d_ste[] = { 0x00, 0x00 };

    lcd_cmd(ST7789_SWRESET);
    sleep_ms(150);

    lcd_cmd_data(ST7789_COLMOD, d_colmod, sizeof(d_colmod));
    lcd_cmd_data(ST7789_PORCTRL, d_porctrl, sizeof(d_porctrl));
    lcd_cmd_data(ST7789_LCMCTRL, d_lcmctrl, sizeof(d_lcmctrl));
    lcd_cmd_data(ST7789_VDVVRHEN, d_vdvvrhen, sizeof(d_vdvvrhen));
    lcd_cmd_data(ST7789_VRHS, d_vrhs, sizeof(d_vrhs));
    lcd_cmd_data(ST7789_VDVS, d_vdvs, sizeof(d_vdvs));
    lcd_cmd_data(ST7789_PWCTRL1, d_pwctrl1, sizeof(d_pwctrl1));
    lcd_cmd_data(ST7789_FRCTRL2, d_frctrl2, sizeof(d_frctrl2));
    lcd_cmd_data(ST7789_RAMCTRL, d_ramctrl, sizeof(d_ramctrl));
    lcd_cmd_data(ST7789_GCTRL, d_gctrl, sizeof(d_gctrl));
    lcd_cmd_data(ST7789_VCOMS, d_vcoms, sizeof(d_vcoms));
    lcd_cmd_data(ST7789_GMCTRP1, d_gmctrp1, sizeof(d_gmctrp1));
    lcd_cmd_data(ST7789_GMCTRN1, d_gmctrn1, sizeof(d_gmctrn1));
    lcd_cmd(ST7789_INVON);
    lcd_cmd(ST7789_SLPOUT);
    sleep_ms(100);

    lcd_cmd_data(ST7789_CASET, d_caset, sizeof(d_caset));
    lcd_cmd_data(ST7789_RASET, d_raset, sizeof(d_raset));
    lcd_cmd_data(ST7789_MADCTL, d_madctl, sizeof(d_madctl));

    // Clear the whole GRAM to black: RAMWR, then hold CS low while a
    // single zero byte is clocked out with DMA read increment off (as in
    // the reference), then restore read increment.
    lcd_wait_idle();
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    lcd_put_byte(ST7789_RAMWR);
    lcd_wait_idle();
    gpio_put(LCD_PIN_DC, 1);
    linebuf[0][0] = 0;
    lcd_configure_dma(false);
    lcd_write_blocking((const uint8_t *)linebuf,
        (size_t)LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
    lcd_configure_dma(true);
    gpio_put(LCD_PIN_CS, 1);

    lcd_cmd_data(ST7789_TEON, d_teon, sizeof(d_teon));
    lcd_cmd_data(ST7789_STE, d_ste, sizeof(d_ste));
    lcd_cmd(ST7789_DISPON);

    gpio_put(LCD_PIN_BL, 1);
}

// Expand one framebuffer row to RGB565. Stored byte-swapped so the DMA byte
// stream puts the high byte on the bus first, as the ST7789 expects for
// RAMWR data.
static void convert_line(int y, uint16_t *dst) {
    const uint8_t *src = graphics_buffer + y * (LCD_WIDTH / 2);
    for (int x = 0; x < LCD_WIDTH; x += 2) {
        uint8_t b = src[x >> 1];
        dst[x] = (uint16_t)__builtin_bswap16(palette_rgb565[b & 0x0F]);
        dst[x + 1] = (uint16_t)__builtin_bswap16(palette_rgb565[b >> 4]);
    }
}

uint8_t *graphics_get_buffer(void) {
    return graphics_buffer;
}

uint32_t get_frame_count(void) {
    return graphics_frame_count;
}

uint32_t graphics_get_width(void) {
    return graphics_width;
}

uint32_t graphics_get_height(void) {
    return graphics_height;
}

void graphics_set_res(int w, int h) {
    graphics_width = (uint32_t)w;
    graphics_height = (uint32_t)h;
}

void graphics_set_shift(int x, int y) {
    graphics_shift_x = x;
    graphics_shift_y = y;
}

struct video_mode_t graphics_get_video_mode(int mode) {
    (void)mode;
    struct video_mode_t m = { 0, 0, 0, 0 };
    return m;
}

void graphics_set_palette(uint8_t i, uint32_t color888) {
    // Only 16 entries exist in the 4bpp path; main.c also fills 16..255 with
    // greys for the 8bpp HDMI palette, and those must not wrap onto 0..15.
    if (i > 15)
        return;
    palette_rgb565[i] = rgb888_to_rgb565(color888);
}

void graphics_set_bgcolor(uint32_t color888) {
    bgcolor_rgb565 = rgb888_to_rgb565(color888);
}

void graphics_restore_sync_colors(void) {
    // No-op: only meaningful for HDMI scanout timing.
}

void set_palette(uint8_t n) {
    uint8_t idx = n % 11;
    for (int i = 0; i < 16; i++) {
        graphics_set_palette((uint8_t)i, tab_color[idx][i]);
    }
}

void startVIDEO(uint8_t vol) {
    // No-op: the panel is already running after graphics_init().
    (void)vol;
}

void graphics_init(g_out g) {
    (void)g;
    uint offset;

    gpio_set_function(LCD_PIN_DC, GPIO_FUNC_SIO);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_put(LCD_PIN_DC, 0);

    gpio_set_function(LCD_PIN_CS, GPIO_FUNC_SIO);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_put(LCD_PIN_CS, 1);

    gpio_init(LCD_PIN_BL);
    gpio_set_dir(LCD_PIN_BL, GPIO_OUT);
    gpio_put(LCD_PIN_BL, 0);

    // Tearing-effect output from the panel: input only, never drive it.
    gpio_init(LCD_PIN_TE);
    gpio_set_dir(LCD_PIN_TE, GPIO_IN);

    // PIO setup, mirroring the reference ST7789 constructor. The data pins
    // are above GPIO31, so the GPIO base goes first; absolute GPIO numbers
    // are passed to every SDK call and the SDK subtracts the base itself.
    pio_set_gpio_base(LCD_PIO, LCD_PIO_GPIO_BASE);
    lcd_sm = pio_claim_unused_sm(LCD_PIO, true);
    offset = pio_add_program(LCD_PIO, &par_lcd_program);

    pio_gpio_init(LCD_PIO, LCD_PIN_WR);
    for (uint i = 0; i < 8; i++) {
        pio_gpio_init(LCD_PIO, LCD_PIN_D0 + i);
    }

    gpio_set_function(LCD_PIN_RD, GPIO_FUNC_SIO);
    gpio_set_dir(LCD_PIN_RD, GPIO_OUT);
    gpio_put(LCD_PIN_RD, 1);

    pio_sm_set_consecutive_pindirs(LCD_PIO, lcd_sm, LCD_PIN_D0, 8, true);
    pio_sm_set_consecutive_pindirs(LCD_PIO, lcd_sm, LCD_PIN_WR, 1, true);

    pio_sm_config c = par_lcd_program_get_default_config(offset);
    sm_config_set_out_pins(&c, LCD_PIN_D0, 8);
    sm_config_set_sideset_pins(&c, LCD_PIN_WR);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c, false, true, 8);

    // PIO clock divider, exactly as the reference: two PIO cycles per byte,
    // so the divider counts in half-cycle steps off twice the sys/ceiling
    // ratio, clamped to a minimum sys clock of 1 Hz.
    lcd_clkdiv = ceilf(2.0f * fmaxf(1.0f,
        (float)clock_get_hz(clk_sys) / (float)LCD_PIO_MAX_HZ)) * 0.5f;
    sm_config_set_clkdiv(&c, lcd_clkdiv);

    pio_sm_init(LCD_PIO, lcd_sm, offset, &c);
    pio_sm_set_enabled(LCD_PIO, lcd_sm, true);

    dma_lcd = dma_claim_unused_channel(true);
    lcd_configure_dma(true);

    {
        uint32_t sys_hz = clock_get_hz(clk_sys);
        uint32_t byte_hz = (uint32_t)((float)sys_hz / (lcd_clkdiv * 2.0f));
        MII_DEBUG_PRINTF("PAR LCD: pio=%d sm=%u clkdiv=%.2f sys=%u Hz byte rate ~%u B/s\n",
            pio_get_index(LCD_PIO), lcd_sm, (double)lcd_clkdiv, sys_hz, byte_hz);
    }

    lcd_init_panel();
    MII_DEBUG_PRINTF("PAR LCD: ST7789 init done (backlight on)\n");
}

void graphics_present(void) {
    static const uint8_t d_caset[] = { 0x00, 0x00, 0x01, 0x3F }; // 0..319
    static const uint8_t d_raset[] = { 0x00, 0x00, 0x00, 0xEF }; // 0..239
    uint32_t te_start = time_us_32();

    // Wait for the tearing-effect signal, bounded so a missing TE line
    // never hangs the frame.
    while (!gpio_get(LCD_PIN_TE)) {
        if ((time_us_32() - te_start) > LCD_TE_TIMEOUT_US) {
            break;
        }
    }

    lcd_cmd_data(ST7789_CASET, d_caset, sizeof(d_caset));
    lcd_cmd_data(ST7789_RASET, d_raset, sizeof(d_raset));

    // RAMWR, then hold CS low for the whole frame.
    lcd_cmd(ST7789_RAMWR);
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);

    convert_line(0, linebuf[0]);
    lcd_start_dma((const uint8_t *)linebuf[0], LCD_LINE_BYTES);
    for (int y = 1; y < LCD_HEIGHT; y++) {
        convert_line(y, linebuf[y & 1]);
        dma_channel_wait_for_finish_blocking((uint)dma_lcd);
        lcd_start_dma((const uint8_t *)linebuf[y & 1], LCD_LINE_BYTES);
    }
    lcd_wait_idle();
    gpio_put(LCD_PIN_CS, 1);

    graphics_frame_count++;
}

uint32_t hdmi_get_irq_count(void) {
    return 0;
}

bool hdmi_check_and_restart(void) {
    // No scanout engine to watch; never needs a restart.
    return false;
}

void hdmi_pause(void) {
}

void hdmi_resume(void) {
}

void graphics_set_defer_irq_to_core1(bool defer) {
    (void)defer;
}

void graphics_init_irq_on_this_core(void) {
}

void graphics_rebind_irq_to_current_core(void) {
}
