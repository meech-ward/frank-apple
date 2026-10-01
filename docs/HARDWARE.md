# Supported downstream hardware

## Pico 2 W + Pimoroni Display Pack 2.8

This variant expects the RP2350 **Pico 2 W**, the Display Pack 2.8's ST7789
320×240 screen, and a FAT-formatted SD card. It does not require PSRAM.

| Signal | GPIO |
| --- | --- |
| LCD DC / CS / SCK / MOSI / backlight | 16 / 17 / 18 / 19 / 20 |
| SD MISO / CS / SCK / MOSI | 8 / 9 / 10 / 11 |
| UART TX / RX, optional debug | 0 / 1 |

The SD module uses the Display Pack's SP/CE connector; see
[wiring](wiring-sd.svg) and [connector wiring](wiring-sd-plug.svg).
Use 3.3 V-compatible SD wiring and a shared ground. The display uses hardware
SPI0, while the card uses PIO SPI. Do not assign the radio's internal pins to
external peripherals. Display Pack A/B/X/Y buttons are currently unmapped.

## Pimoroni Tufty 2350

The Tufty variant uses its built-in parallel ST7789 screen, 8 MiB PSRAM, and
16 MiB flash. Firmware occupies the first 4 MiB; the final 12 MiB hold a FAT16
volume. There is no SD-card slot. Board pin definitions live in
`boards/pimoroni_tufty2350.h` and `src/board_config.h`.

HOME opens/closes the [button launcher](BUTTON-LAUNCHER.md); up/down move,
A goes back, and B or C confirms. Saved programs browses Applesoft files on the
DOS 3.3 disk in drive 1; Choose a disk opens the disk menu. Select Web control
on the HOME screen with B or C to toggle browser control. Outside the menu,
A sends Escape, B Return, C Right, and up/down send their arrows.
The badge has no built-in speaker.

## USB role and recovery

Choose **console** for USB serial input/debugging and browser typing. Choose
**keyboard** for a USB keyboard connected through an appropriate powered OTG
adapter. The native port does not serve as USB serial while hosting a keyboard.

With a keyboard, F11 opens the launcher, Space toggles web control on its first
screen, and F11 returns to the Apple. The console equivalent is Ctrl-], Space,
Ctrl-]. Web control starts off after every restart.

Both variants retain SWD debugging. To switch firmware, use the bootloader
(BOOTSEL on Pico, BOOT + RESET on Tufty) or a Debug Probe. Firmware-only updates
preserve existing disk storage. No cloud connection is needed for recovery.
