// SPDX-License-Identifier: MIT
//
// 1 bpp framebuffer for the 2.9" ST7305 panel, used in portrait orientation.
// Drawing coordinates: (0,0) = top left, x to the right, y down.
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_WIDTH 168
#define DISPLAY_HEIGHT 384

esp_err_t display_init(void);
esp_err_t display_flush(void);

void display_clear(void);
void display_invert(void);
void display_pixel(int x, int y);
void display_unpixel(int x, int y);
void display_fill_rect(int x, int y, int w, int h);
void display_rect(int x, int y, int w, int h, int thickness);
void display_line(int x0, int y0, int x1, int y1);

#ifdef __cplusplus
}
#endif
