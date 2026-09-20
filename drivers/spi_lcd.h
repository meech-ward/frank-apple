#pragma once
#ifndef SPI_LCD_H_
#define SPI_LCD_H_

// ST7789 SPI display driver (Pico 2 W + Pimoroni Display Pack 2.8).
// Implements the same video API as drivers/HDMI.h so the emulator links
// with the HDMI and VGA sources absent. Pushes the shared 320x240 4bpp
// framebuffer to the panel when graphics_present() is called.

#include "HDMI.h"

void graphics_present(void);

#endif
