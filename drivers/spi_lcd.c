/*
 * spi_lcd.c - ST7789 SPI video driver (Pico 2 W + Pimoroni Display Pack 2.8)
 *
 * Same framebuffer contract as drivers/HDMI.c: 320x240, 4 bits per pixel,
 * packed two pixels per byte (38,400 bytes). The LOW nibble of each byte is
 * the LEFT pixel of the pair, the HIGH nibble is the RIGHT pixel (see
 * mii_video.c: even x goes to the low nibble, odd x is ORed in shifted
 * left by 4; HDMI.c and vga.c scanout both emit the low nibble first).
 *
 * Unlike HDMI/VGA there is no continuous scanout: graphics_present() pushes
 * one full frame to the panel on demand. It expands the 4-bit framebuffer
 * through the 16-entry palette into RGB565 one line at a time using two
 * 640-byte line buffers, converting line n+1 while DMA sends line n.
 */

#include "../src/board_config.h"
#include "spi_lcd.h"
#include "debug_log.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define LCD_LINE_BYTES (LCD_WIDTH * 2)

// ST7789 commands
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
#define ST7789_RAMCTRL 0xB0
#define ST7789_PORCTRL 0xB2
#define ST7789_LCMCTRL 0xC0
#define ST7789_VDVVRHEN 0xC2
#define ST7789_VRHS 0xC3
#define ST7789_VDVS 0xC4
#define ST7789_FRCTRL2 0xC6
#define ST7789_PWCTRL1 0xD0

// MADCTL for 320x240 landscape: MX|MV|ML.
// NOTE: if the picture is later found mirrored on hardware, this byte is
// the fix (adjust the MX/MV/ML bits here).
#define ST7789_MADCTL_LANDSCAPE 0x70

// Requested SPI clock; the achieved rate is logged at init.
#define LCD_SPI_BAUD_TARGET (62500000u)

static uint8_t graphics_buffer[LCD_WIDTH * LCD_HEIGHT / 2] __aligned(4096) = { 0 };

static uint16_t palette_rgb565[16] = { 0 };
static uint16_t bgcolor_rgb565 = 0;

static uint32_t graphics_width = LCD_WIDTH;
static uint32_t graphics_height = LCD_HEIGHT;
static int graphics_shift_x = 0;
static int graphics_shift_y = 0;

static volatile uint32_t graphics_frame_count = 0;

static int dma_lcd = -1;
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

static void lcd_cs(bool level) {
    gpio_put(LCD_PIN_CS, level);
}

static void lcd_dc(bool level) {
    gpio_put(LCD_PIN_DC, level);
}

static void lcd_cmd(uint8_t cmd) {
    lcd_dc(false);
    lcd_cs(false);
    spi_write_blocking(LCD_SPI, &cmd, 1);
    lcd_cs(true);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {
    lcd_dc(false);
    lcd_cs(false);
    spi_write_blocking(LCD_SPI, &cmd, 1);
    if (len) {
        lcd_dc(true);
        spi_write_blocking(LCD_SPI, data, len);
    }
    lcd_cs(true);
}

// Pimoroni init sequence (from pimoroni-pico drivers/st7789/st7789.cpp).
// No reset pin on the Display Pack 2.8, so SWRESET is used instead.
static void lcd_init_panel(void) {
    static const uint8_t d_porctrl[] = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
    static const uint8_t d_lcmctrl[] = { 0x2C };
    static const uint8_t d_vdvvrhen[] = { 0x01 };
    static const uint8_t d_vrhs[] = { 0x12 };
    static const uint8_t d_vdvs[] = { 0x20 };
    static const uint8_t d_pwctrl1[] = { 0xA4, 0xA1 };
    static const uint8_t d_frctrl2[] = { 0x0F };
    static const uint8_t d_ramctrl[] = { 0x00, 0xC0 };
    static const uint8_t d_colmod[] = { 0x05 }; // RGB565
    static const uint8_t d_madctl[] = { ST7789_MADCTL_LANDSCAPE };

    lcd_cmd(ST7789_SWRESET);
    sleep_ms(150);

    lcd_cmd(ST7789_TEON);
    lcd_cmd_data(ST7789_COLMOD, d_colmod, sizeof(d_colmod));
    lcd_cmd_data(ST7789_PORCTRL, d_porctrl, sizeof(d_porctrl));
    lcd_cmd_data(ST7789_LCMCTRL, d_lcmctrl, sizeof(d_lcmctrl));
    lcd_cmd_data(ST7789_VDVVRHEN, d_vdvvrhen, sizeof(d_vdvvrhen));
    lcd_cmd_data(ST7789_VRHS, d_vrhs, sizeof(d_vrhs));
    lcd_cmd_data(ST7789_VDVS, d_vdvs, sizeof(d_vdvs));
    lcd_cmd_data(ST7789_PWCTRL1, d_pwctrl1, sizeof(d_pwctrl1));
    lcd_cmd_data(ST7789_FRCTRL2, d_frctrl2, sizeof(d_frctrl2));
    lcd_cmd_data(ST7789_RAMCTRL, d_ramctrl, sizeof(d_ramctrl));
    lcd_cmd(ST7789_INVON);
    lcd_cmd(ST7789_SLPOUT);
    lcd_cmd(ST7789_DISPON);
    sleep_ms(100);

    lcd_cmd_data(ST7789_MADCTL, d_madctl, sizeof(d_madctl));

    gpio_put(LCD_PIN_BL, 1);
}

// Expand one framebuffer row to RGB565. Stored byte-swapped so the DMA
// stream (8-bit frames, MSB first) puts the high byte on the wire first,
// as the ST7789 expects for RAMWR data.
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

    gpio_init(LCD_PIN_DC);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_put(LCD_PIN_DC, 0);

    gpio_init(LCD_PIN_CS);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_put(LCD_PIN_CS, 1);

    gpio_init(LCD_PIN_BL);
    gpio_set_dir(LCD_PIN_BL, GPIO_OUT);
    gpio_put(LCD_PIN_BL, 0);

    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);

    spi_init(LCD_SPI, LCD_SPI_BAUD_TARGET);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    uint actual = spi_set_baudrate(LCD_SPI, LCD_SPI_BAUD_TARGET);
    MII_DEBUG_PRINTF("SPI LCD: requested %u Hz, achieved %u Hz\n",
        LCD_SPI_BAUD_TARGET, actual);

    dma_lcd = dma_claim_unused_channel(true);

    dma_channel_config cfg = dma_channel_get_default_config(dma_lcd);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg,
        spi_get_index(LCD_SPI) == 0 ? DREQ_SPI0_TX : DREQ_SPI1_TX);
    dma_channel_configure(dma_lcd, &cfg,
        &spi_get_hw(LCD_SPI)->dr, linebuf[0], LCD_LINE_BYTES, false);

    lcd_init_panel();
    MII_DEBUG_PRINTF("SPI LCD: ST7789 init done (backlight on)\n");
}

void graphics_present(void) {
    static const uint8_t d_caset[] = { 0x00, 0x00, 0x01, 0x3F };
    static const uint8_t d_raset[] = { 0x00, 0x00, 0x00, 0xEF };

    lcd_cmd_data(ST7789_CASET, d_caset, sizeof(d_caset));
    lcd_cmd_data(ST7789_RASET, d_raset, sizeof(d_raset));

    // RAMWR, then hold CS low for the whole frame.
    lcd_cmd(ST7789_RAMWR);
    lcd_dc(true);
    lcd_cs(false);

    convert_line(0, linebuf[0]);
    dma_channel_set_read_addr(dma_lcd, linebuf[0], true);
    for (int y = 1; y < LCD_HEIGHT; y++) {
        convert_line(y, linebuf[y & 1]);
        dma_channel_wait_for_finish_blocking(dma_lcd);
        dma_channel_set_read_addr(dma_lcd, linebuf[y & 1], true);
    }
    dma_channel_wait_for_finish_blocking(dma_lcd);
    while (spi_is_busy(LCD_SPI)) {
        tight_loop_contents();
    }
    lcd_cs(true);

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
