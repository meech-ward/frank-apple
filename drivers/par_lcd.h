#pragma once
#ifndef PAR_LCD_H_
#define PAR_LCD_H_

// ST7789 8-bit parallel (8080) display driver for the Pimoroni Tufty 2350.
// Implements the same video API as drivers/HDMI.h (see drivers/spi_lcd.c for
// the SPI sibling): a shared 320x240 4bpp framebuffer that graphics_present()
// pushes to the panel through PIO2 + DMA.

#include "HDMI.h"

void graphics_present(void);

#endif
