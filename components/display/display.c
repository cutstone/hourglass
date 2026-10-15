// SPDX-License-Identifier: MIT
#include "display.h"

#include <stdlib.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7305.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define LCD_HOST SPI2_HOST

// The panel driver takes a landscape framebuffer (PANEL_W x PANEL_H), 1 bit/px,
// packed vertically in pages of 8 lines: byte = (y / 8) * PANEL_W + x, bit = y % 8.
#define PANEL_W ST7305_WIDTH  // 384
#define PANEL_H ST7305_HEIGHT // 168
#define PANEL_FB_SIZE (PANEL_W * (PANEL_H / 8))

static const char *TAG = "display";

static uint8_t s_fb[PANEL_FB_SIZE];
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;

// Map portrait drawing coordinates to the panel's landscape framebuffer.
// The panel is also mirrored along its long axis, so x is flipped there.
static inline int fb_index(int x, int y, uint8_t *bit)
{
#if CONFIG_HOURGLASS_LCD_ROTATE_180
    x = DISPLAY_WIDTH - 1 - x;
    y = DISPLAY_HEIGHT - 1 - y;
#endif
    int px = PANEL_W - 1 - y; // portrait y runs along the panel's long axis (mirrored)
    int py = DISPLAY_WIDTH - 1 - x;
    *bit = (uint8_t)(1u << (py % 8));
    return (py / 8) * PANEL_W + px;
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
    spi_bus_config_t bus_cfg = ST7305_PANEL_BUS_SPI_CONFIG(
        CONFIG_HOURGLASS_LCD_PIN_SCK, CONFIG_HOURGLASS_LCD_PIN_MOSI, PANEL_FB_SIZE + 8);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = ST7305_PANEL_IO_SPI_CONFIG(
        CONFIG_HOURGLASS_LCD_PIN_CS, CONFIG_HOURGLASS_LCD_PIN_DC, NULL, NULL);
    io_cfg.pclk_hz = CONFIG_HOURGLASS_LCD_PIXEL_CLOCK_MHZ * 1000 * 1000;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io), TAG,
        "panel io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_HOURGLASS_LCD_PIN_RST,
        .bits_per_pixel = ST7305_BITS_PER_PIXEL,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7305(s_io, &panel_cfg, &s_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on");

#if CONFIG_HOURGLASS_LCD_HIGH_POWER
    // The driver's init leaves the panel in low power mode (slow refresh).
    // Switch to high power mode with a 32 Hz frame rate for smooth animation.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xD8, (uint8_t[]){0xA6, 0xE9}, 2), TAG,
                        "osc");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xB2, (uint8_t[]){0x12}, 1), TAG,
                        "frame rate");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x38, NULL, 0), TAG, "high power");
#endif

    display_clear();
    ESP_LOGI(TAG, "ST7305 ready (%dx%d portrait)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    return display_flush();
}

esp_err_t display_flush(void)
{
    if (!s_panel) {
        return ESP_ERR_INVALID_STATE;
    }
    // The driver always sends the whole framebuffer, regardless of the area given.
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_W, PANEL_H, s_fb);
}
