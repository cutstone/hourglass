// SPDX-License-Identifier: MIT
#include "display.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "st7305.h"

static const char *TAG = "display";

// The drawing coordinates are the panel's native orientation, so the framebuffer
// goes to the driver as it is (see st7305.h for the layout).
static uint8_t s_fb[ST7305_FB_SIZE];
static bool s_ready;

static inline int fb_index(int x, int y, uint8_t *bit)
{
#if CONFIG_HOURGLASS_LCD_ROTATE_180
    x = DISPLAY_WIDTH - 1 - x;
    y = DISPLAY_HEIGHT - 1 - y;
#endif
    *bit = (uint8_t)(1u << (y % 8));
    return (y / 8) * DISPLAY_WIDTH + x;
}

void display_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void display_invert(void)
{
    for (size_t i = 0; i < sizeof(s_fb); i++) {
        s_fb[i] = (uint8_t)~s_fb[i];
    }
}

void display_pixel(int x, int y)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }
    uint8_t bit;
    s_fb[fb_index(x, y, &bit)] |= bit;
}

void display_unpixel(int x, int y)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }
    uint8_t bit;
    s_fb[fb_index(x, y, &bit)] &= (uint8_t)~bit;
}

void display_fill_rect(int x, int y, int w, int h)
{
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            display_pixel(x + col, y + row);
        }
    }
}

void display_rect(int x, int y, int w, int h, int thickness)
{
    if (thickness < 1) {
        thickness = 1;
    }
    display_fill_rect(x, y, w, thickness);                 // top
    display_fill_rect(x, y + h - thickness, w, thickness); // bottom
    display_fill_rect(x, y, thickness, h);                 // left
    display_fill_rect(x + w - thickness, y, thickness, h); // right
}

void display_line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        display_pixel(x0, y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

esp_err_t display_init(void)
{
    const st7305_config_t cfg = {
        .host = SPI2_HOST,
        .pin_sck = CONFIG_HOURGLASS_LCD_PIN_SCK,
        .pin_mosi = CONFIG_HOURGLASS_LCD_PIN_MOSI,
        .pin_cs = CONFIG_HOURGLASS_LCD_PIN_CS,
        .pin_dc = CONFIG_HOURGLASS_LCD_PIN_DC,
        .pin_rst = CONFIG_HOURGLASS_LCD_PIN_RST,
        .pixel_clock_hz = CONFIG_HOURGLASS_LCD_PIXEL_CLOCK_MHZ * 1000 * 1000,
#if CONFIG_HOURGLASS_LCD_HIGH_POWER
        .high_power = true,
#endif
    };
    esp_err_t err = st7305_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    s_ready = true;
    display_clear();
    ESP_LOGI(TAG, "ready (%dx%d portrait)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    return display_flush();
}

esp_err_t display_flush(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return st7305_flush(s_fb);
}
