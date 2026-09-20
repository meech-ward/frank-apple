/*
 * board_config.h
 * 
 * Board configuration for FRANK Apple - Apple IIe emulator for RP2350
 * Based on murmdoom board configuration
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "pico.h"
#include "hardware/structs/sysinfo.h"
#include "hardware/vreg.h"

/*
 * Board Configuration Variants:
 * 
 * BOARD_M1 - M1 GPIO layout
 * BOARD_M2 - M2 GPIO layout
 * 
 * PSRAM pin is auto-detected based on chip package:
 *   RP2350B: GPIO47 (for both M1 and M2)
 *   RP2350A: GPIO19 (M1) or GPIO8 (M2)
 * 
 * M1 GPIO Layout:
 *   HDMI: CLKN=6, CLKP=7, D0N=8, D0P=9, D1N=10, D1P=11, D2N=12, D2P=13
 *   SD:   CLK=2, CMD=3, DAT0=4, DAT3=5
 *   PS/2: CLK=0, DATA=1
 * 
 * M2 GPIO Layout:
 *   HDMI: CLKN=12, CLKP=13, D0N=14, D0P=15, D1N=16, D1P=17, D2N=18, D2P=19
 *   SD:   CLK=6, CMD=7, DAT0=4, DAT3=5
 *   PS/2: CLK=2, DATA=3
 */

// Default to M1 if no config specified
#if !defined(BOARD_M1) && !defined(BOARD_M2) && !defined(BOARD_P2W) && !defined(BOARD_TUFTY)
#define BOARD_M1
#endif

//=============================================================================
// CPU/PSRAM Speed Defaults (can be overridden via CMake)
//=============================================================================
#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ 252
#endif

#ifndef CPU_VOLTAGE
#define CPU_VOLTAGE VREG_VOLTAGE_1_50
#endif

//=============================================================================
// PSRAM Configuration
//=============================================================================

// PSRAM pin for RP2350A variants
#ifdef BOARD_M1
#define PSRAM_PIN_RP2350A 19
#else
#define PSRAM_PIN_RP2350A 8
#endif

// PSRAM pin for RP2350B (always GPIO47)
#define PSRAM_PIN_RP2350B 47

// Runtime function to get PSRAM pin based on chip package
static inline uint get_psram_pin(void) {
#if PICO_RP2040
    return 0;
#endif
#if PICO_RP2350
#ifdef BOARD_TUFTY
    return 8;  // Tufty 2350: 8 MB PSRAM chip select on GPIO8 (RP2350B, not the M1/M2 GPIO47 layout)
#else
    uint32_t package_sel = *((io_ro_32*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET));
    if (package_sel & 1) {
        return PSRAM_PIN_RP2350A;
    } else {
        return PSRAM_PIN_RP2350B;
    }
#endif
#endif
}

//=============================================================================
// M1 Layout Configuration
//=============================================================================
#ifdef BOARD_M1

// HDMI Pins
#define HDMI_PIN_CLKN 6
#define HDMI_PIN_CLKP 7
#define HDMI_PIN_D0N  8
#define HDMI_PIN_D0P  9
#define HDMI_PIN_D1N  10
#define HDMI_PIN_D1P  11
#define HDMI_PIN_D2N  12
#define HDMI_PIN_D2P  13

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    2
#define SDCARD_PIN_CMD    3
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  0
#define PS2_PIN_DATA 1

// NES/SNES Gamepad Pins (directly after HDMI pins)
#define NESPAD_GPIO_CLK   14
#define NESPAD_GPIO_DATA  16
#define NESPAD_GPIO_LATCH 15

// I2S Audio Pins
#define I2S_DATA_PIN       26
#define I2S_CLOCK_PIN_BASE 27

#define PWM_RIGHT_PIN 26
#define PWM_LEFT_PIN 27
#define BEEPER_PIN 28

#define PSRAM
#define PSRAM_SPINLOCK 1
#define PSRAM_ASYNC 1

#define PSRAM_PIN_CS 18
#define PSRAM_PIN_SCK 19
#define PSRAM_PIN_MOSI 20
#define PSRAM_PIN_MISO 21

#endif // BOARD_M1

//=============================================================================
// M2 Layout Configuration
//=============================================================================
#ifdef BOARD_M2

// HDMI Pins
#define HDMI_PIN_CLKN 12
#define HDMI_PIN_CLKP 13
#define HDMI_PIN_D0N  14
#define HDMI_PIN_D0P  15
#define HDMI_PIN_D1N  16
#define HDMI_PIN_D1P  17
#define HDMI_PIN_D2N  18
#define HDMI_PIN_D2P  19

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    6
#define SDCARD_PIN_CMD    7
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  2
#define PS2_PIN_DATA 3

// NES/SNES Gamepad Pins (using available GPIOs)
#define NESPAD_GPIO_CLK   20
#define NESPAD_GPIO_DATA  22
#define NESPAD_GPIO_LATCH 21

// I2S Audio Pins
#define I2S_DATA_PIN       9
#define I2S_CLOCK_PIN_BASE 10

#define BEEPER_PIN 9
#define PWM_RIGHT_PIN 10
#define PWM_LEFT_PIN 11

//#define PSRAM
#define PSRAM_SPINLOCK 1
#define PSRAM_ASYNC 1

#define PSRAM_PIN_CS 8
#define PSRAM_PIN_SCK 6
#define PSRAM_PIN_MOSI 7
#define PSRAM_PIN_MISO 4

#endif // BOARD_M2

//=============================================================================
// P2W Layout Configuration (Pico 2 W + Pimoroni Display Pack 2.8)
//=============================================================================
// ST7789 320x240 on hardware SPI0; SD card on PIO-SPI (pio1, sm 0) since
// hardware SPI0 belongs to the display. Pico 2 W radio uses GP23/24/25/29
// internally, so those are never assigned here. No PSRAM in this variant.
#ifdef BOARD_P2W

// ST7789 Display Pins (hardware SPI0)
#define LCD_PIN_DC   16
#define LCD_PIN_CS   17
#define LCD_PIN_SCK  18
#define LCD_PIN_MOSI 19
#define LCD_PIN_BL   20
#define LCD_SPI      spi0

// Display Pack 2.8 buttons
#define BTN_A_PIN 12
#define BTN_B_PIN 13
#define BTN_X_PIN 14
#define BTN_Y_PIN 15

// SD Card Pins (PIO-SPI, see drivers/sdcard/spi.pio)
// SM 1: the NES pad driver claims the first free machine on pio1 (SM 0) before the SD init.
// SD card on the Display Pack 2.8 SP/CE plug: pin 3=GP8 MISO, 4=GP11 MOSI, 5=GP10 SCK, 6=GP9 CS,
// pin 1 GND, 7 3V3, 8 VSYS (5 V), pin 2=GP7 is the UPS-B I2C SCL: leave it unconnected.
// Fallback if wiring to the header instead: CLK 2, CMD 3, D0 4, D3 5 (GP4/5 double as Qw/ST).
#define SDCARD_PIN_CLK 10
#define SDCARD_PIN_CMD 11
#define SDCARD_PIN_D0  8
#define SDCARD_PIN_D3  9
// NES pad pins 9/10/11 are the SP/CE SPI lines; no pad on this board, keep the driver off them.
#define NESPAD_DISABLED 1
#define SDCARD_PIO    pio1
#define SDCARD_PIO_SM 1

// PS/2 Keyboard Pins (off in our build; UART0 debug shares them)
#define PS2_PIN_CLK  0
#define PS2_PIN_DATA 1

// NES/SNES Gamepad Pins
#define NESPAD_GPIO_CLK   9
#define NESPAD_GPIO_DATA  10
#define NESPAD_GPIO_LATCH 11

// I2S Audio Pins (unused)
// I2S (MAX98357A): DIN GP22, BCLK GP2, LRCLK GP3 (clock pins must be adjacent). Build with -DAUDIO_TYPE=I2S.
#define I2S_DATA_PIN       22
#define I2S_CLOCK_PIN_BASE 2

// PWM Audio Pins
// GP21 is wired to the LCD TE output on Display Pack 2.8 (via 330R): never drive it.
// GP22 is the only header pin with nothing attached (Display Pack + UPS-B).
#define PWM_RIGHT_PIN 22
#define PWM_LEFT_PIN  22
#define BEEPER_PIN 22

// No PSRAM / PSRAM_* defines in this variant.

#endif // BOARD_P2W

//=============================================================================
// TUFTY Layout Configuration (Pimoroni Tufty 2350 badge, RP2350B)
//=============================================================================
// ST7789 320x240 on an 8-bit 8080 parallel bus driven by PIO2 (GPIO base 16 so
// the data pins 32-39 are reachable). No SD card: FatFs runs on a region of the
// 16 MB flash (drivers/flashdisk). 8 MB PSRAM on CS GPIO8. Radio (CYW43) on
// 23/24/25/29 exactly like the Pico W, so the cyw43 driver is unchanged.
// Pin table: pimoroni/tufty2350 board/pins.csv.
#ifdef BOARD_TUFTY

// ST7789 parallel display
#define LCD_PIN_BL   26
#define LCD_PIN_CS   27
#define LCD_PIN_DC   28   // "RS" on the badge schematic
#define LCD_PIN_WR   30   // PIO side-set: write strobe
#define LCD_PIN_RD   31   // held high (never read from the panel)
#define LCD_PIN_D0   32   // DB0..DB7 = GPIO32..39
#define LCD_PIN_TE   21   // panel tearing-effect output: input only, never drive it
#define LCD_PIO      pio2
#define LCD_PIO_GPIO_BASE 16
#define LCD_PIO_MAX_HZ (44u * 1000u * 1000u)   // Pimoroni's ceiling for the parallel PIO clock

// Buttons: active low, pull-ups on the badge
#define BTN_DOWN_PIN 6
#define BTN_A_PIN    7
#define BTN_B_PIN    9
#define BTN_C_PIN    10
#define BTN_UP_PIN   11
#define BTN_HOME_PIN 22

// Rear white case LEDs, GPIO0..3: the disk activity light
#define CASE_LED_MASK 0x0Fu

// Power
#define POWER_EN_PIN    41   // hold high so the badge stays on when running from the LiPo
#define VBUS_DETECT_PIN 12
#define VBAT_SENSE_PIN  40

// Flash-resident FAT volume (drivers/flashdisk): the last 12 MB of the 16 MB flash.
// The firmware is linked into the first 4 MB (memmap.ld FLASH LENGTH = 4096k).
#define FLASHDISK_ENABLED 1
#define FLASHDISK_OFFSET  (4u * 1024u * 1024u)
#define FLASHDISK_SIZE    (12u * 1024u * 1024u)

// No NES pad, no PS/2, no speaker: keep those drivers on GPIOs that have no pad on the badge.
#define NESPAD_DISABLED 1
#define NESPAD_GPIO_CLK   16
#define NESPAD_GPIO_DATA  17
#define NESPAD_GPIO_LATCH 18
#define PS2_PIN_CLK  16
#define PS2_PIN_DATA 17
#define I2S_DATA_PIN       16
#define I2S_CLOCK_PIN_BASE 17
#define PWM_RIGHT_PIN 16
#define PWM_LEFT_PIN  16
#define BEEPER_PIN    16

#endif // BOARD_TUFTY

//=============================================================================
// Apple IIe Display Configuration
//=============================================================================

// Apple II native resolution is 280x192 (HiRes) or 560x192 (DHR)
// We'll scale to fit 640x480 HDMI output
#define APPLE2_HIRES_WIDTH   280
#define APPLE2_HIRES_HEIGHT  192
#define APPLE2_DHR_WIDTH     560
#define APPLE2_DHR_HEIGHT    192

// HDMI output resolution
#define HDMI_WIDTH  640
#define HDMI_HEIGHT 480

// Apple II framebuffer (8-bit indexed color)
#define APPLE2_FB_WIDTH  560
#define APPLE2_FB_HEIGHT 384  // 192 * 2 for scanline doubling

#endif // BOARD_CONFIG_H

//=============================================================================
// Disk-activity LED: Pico W / Pico 2 W route the LED through the CYW43, so
// PICO_DEFAULT_LED_PIN is undefined there. Make the LED a no-op in that case.
//=============================================================================
#include "hardware/gpio.h"
#if defined(BOARD_TUFTY)
// Tufty 2350: the four rear case LEDs (GPIO0..3) are the drive light.
#define FRANK_LED_PUT(v) gpio_put_masked(CASE_LED_MASK, (v) ? CASE_LED_MASK : 0u)
#define FRANK_LED_INIT() do { gpio_init_mask(CASE_LED_MASK); gpio_set_dir_out_masked(CASE_LED_MASK); gpio_put_masked(CASE_LED_MASK, 0u); } while (0)
#elif defined(PICO_DEFAULT_LED_PIN)
#define FRANK_LED_PUT(v) gpio_put(PICO_DEFAULT_LED_PIN, (v))
#define FRANK_LED_INIT() do { gpio_init(PICO_DEFAULT_LED_PIN); gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT); } while (0)
#else
#define FRANK_LED_PUT(v) ((void)0)
#define FRANK_LED_INIT() ((void)0)
#endif
