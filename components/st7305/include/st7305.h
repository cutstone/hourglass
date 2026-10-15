// SPDX-License-Identifier: MIT
//
// Driver for the Sitronix ST7305 reflective monochrome LCD (2.9", 168x384, YDP290H004).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ST7305_WIDTH 168
#define ST7305_HEIGHT 384

// Framebuffer in the panel's native (portrait) orientation: 1 bit per pixel,
// packed vertically in pages of 8 rows: byte = (y / 8) * ST7305_WIDTH + x, bit = y % 8.
// 1 = dark pixel.
#define ST7305_FB_SIZE (ST7305_WIDTH * (ST7305_HEIGHT / 8))

typedef struct {
    int host; // spi_host_device_t, e.g. SPI2_HOST
    int pin_sck;
    int pin_mosi;
    int pin_cs;
    int pin_dc;
    int pin_rst;
    int pixel_clock_hz;
    bool high_power; // 32 Hz refresh instead of 1 Hz
} st7305_config_t;

// Initialise the SPI bus, reset the panel, run the init sequence and clear the panel RAM.
esp_err_t st7305_init(const st7305_config_t *cfg);

// Convert a framebuffer (ST7305_FB_SIZE bytes) to the panel's RAM layout and send it.
// Waits for the previous frame; the new frame is sent with DMA in the background.
esp_err_t st7305_flush(const uint8_t *fb);

#ifdef __cplusplus
}
#endif
