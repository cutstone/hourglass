// SPDX-License-Identifier: MIT
//
// The init sequence is the one verified on the YDP290H004 module (gate voltages and
// auto power down adjusted to its datasheet). It is based on ST7305_2p9_BW_DisplayDriver from
// https://github.com/FT-tele/ST7305_MonoTFT_Arduino_Library (Copyright (c) 2025 FT-未雨, MIT).
#include "st7305.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "st7305";

// Panel RAM: each byte holds a 4x2 pixel block, 42 bytes per row pair, 192 row pairs.
//   bit 7-(2*dx+dy) = pixel (4*col + dx, 2*row + dy)
#define RAM_COLS (ST7305_WIDTH / 4)
#define RAM_ROWS (ST7305_HEIGHT / 2)
#define RAM_SIZE (RAM_COLS * RAM_ROWS)

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[10];
    uint8_t delay_ms;
} init_cmd_t;

static const init_cmd_t init_cmds[] = {
    {0xD6, 2, {0x13, 0x02}, 0},             // NVM load control
    {0xD1, 1, {0x01}, 0},                   // Booster enable
    {0xC0, 2, {0x0B, 0x0A}, 0},             // Gate voltage: VGH 13.5 V, VGL -10 V (datasheet max)
    {0xC1, 4, {0x3C, 0x3E, 0x3C, 0x3C}, 0}, // VSHP 4.8 V
    {0xC2, 4, {0x23, 0x21, 0x23, 0x23}, 0}, // VSLP 0.98 V
    {0xC4, 4, {0x5A, 0x5C, 0x5A, 0x5A}, 0}, // VSHN -3.6 V
    {0xC5, 4, {0x37, 0x35, 0x37, 0x37}, 0}, // VSLN 0.22 V
    {0xD8, 2, {0xA6, 0xE9}, 0},             // OSC enable, HPM max 32 Hz
    {0xB2, 1, {0x12}, 0},                   // Frame rate: HPM 32 Hz, LPM 1 Hz
    {0xB3, 10, {0xE5, 0xF6, 0x17, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x71}, 0}, // HPM gate EQ
    {0xB4, 8, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 0},              // LPM gate EQ
    {0x62, 3, {0x32, 0x03, 0x1F}, 0},                                            // Gate timing
    {0xB7, 1, {0x13}, 0},                                                        // Source EQ
    {0xB0, 1, {0x60}, 0},                                                        // 384 gate lines
    {0x11, 0, {0}, 100},                                                         // Sleep out
    {0xC9, 1, {0x00}, 0},       // Source voltage select
    {0x36, 1, {0x48}, 0},       // MADCTL: MX=1, DO=1
    {0x3A, 1, {0x11}, 0},       // Data format: 3 writes for 24 bit
    {0xB9, 1, {0x20}, 0},       // Mono mode
    {0xB8, 1, {0x29}, 0},       // Panel: 1-dot inversion, one line interlace
    {0x2A, 2, {0x17, 0x24}, 0}, // Column address: 14 * 12 = 168
    {0x2B, 2, {0x00, 0xBF}, 0}, // Row address: 192 * 2 = 384
    {0x35, 1, {0x00}, 0},       // Tearing effect line on
    {0xD0, 1, {0x7F}, 0},       // Auto power down off (with it on, the image collapses)
};

static const init_cmd_t start_cmds[] = {
    {0x29, 0, {0}, 10},    // Display on
    {0x20, 0, {0}, 0},     // Inversion off
    {0xBB, 1, {0x4F}, 10}, // Clear RAM to 0
};

static esp_lcd_panel_io_handle_t s_io;
static uint8_t *s_ram;
static SemaphoreHandle_t s_done;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                          void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static void convert(const uint8_t *fb, uint8_t *ram)
{
    for (int row = 0; row < RAM_ROWS; row++) {
        int y = row * 2;
        const uint8_t *page = fb + (y / 8) * ST7305_WIDTH;
        int shift = y % 8;
        uint8_t *out = ram + row * RAM_COLS;
        for (int col = 0; col < RAM_COLS; col++) {
            uint8_t b = 0;
            for (int dx = 0; dx < 4; dx++) {
                uint8_t px = (page[col * 4 + dx] >> shift) & 0x3; // bit0: row y, bit1: row y+1
                b |= ((px & 1) << (7 - 2 * dx)) | ((px >> 1) << (6 - 2 * dx));
            }
            out[col] = b;
        }
    }
}

static esp_err_t send_cmds(const init_cmd_t *cmds, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const init_cmd_t *c = &cmds[i];
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_io_tx_param(s_io, c->cmd, c->len ? c->data : NULL, c->len), TAG,
            "command 0x%02X", c->cmd);
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }
    return ESP_OK;
}

esp_err_t st7305_init(const st7305_config_t *cfg)
{
    spi_bus_config_t bus = {
        .sclk_io_num = cfg->pin_sck,
        .mosi_io_num = cfg->pin_mosi,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = RAM_SIZE,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(cfg->host, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    s_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_done, ESP_ERR_NO_MEM, TAG, "semaphore");
    xSemaphoreGive(s_done);

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = cfg->pin_cs,
        .dc_gpio_num = cfg->pin_dc,
        .spi_mode = 0,
        .pclk_hz = cfg->pixel_clock_hz,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = on_color_done,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg->host, &io_cfg, &s_io), TAG,
        "panel io");

    s_ram = heap_caps_calloc(1, RAM_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_ram, ESP_ERR_NO_MEM, TAG, "ram buffer");

    gpio_config_t rst = {.pin_bit_mask = 1ULL << cfg->pin_rst, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "reset gpio");
    gpio_set_level(cfg->pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(cfg->pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_RETURN_ON_ERROR(send_cmds(init_cmds, sizeof(init_cmds) / sizeof(init_cmds[0])), TAG,
                        "init");
    // 0x38: high power mode (32 Hz), 0x39: low power mode (1 Hz)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, cfg->high_power ? 0x38 : 0x39, NULL, 0),
                        TAG, "power mode");
    ESP_RETURN_ON_ERROR(send_cmds(start_cmds, sizeof(start_cmds) / sizeof(start_cmds[0])), TAG,
                        "start");

    ESP_LOGI(TAG, "initialised %dx%d, SPI %d Hz, %s power mode", ST7305_WIDTH, ST7305_HEIGHT,
             cfg->pixel_clock_hz, cfg->high_power ? "high" : "low");
    return ESP_OK;
}

esp_err_t st7305_flush(const uint8_t *fb)
{
    // Wait until DMA has finished with the previous frame before reusing the buffer.
    xSemaphoreTake(s_done, portMAX_DELAY);
    convert(fb, s_ram);
    static const uint8_t col[] = {0x17, 0x24};
    static const uint8_t row[] = {0x00, 0xBF};
    esp_err_t err = esp_lcd_panel_io_tx_param(s_io, 0x2A, col, sizeof(col));
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(s_io, 0x2B, row, sizeof(row));
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_color(s_io, 0x2C, s_ram, RAM_SIZE);
    }
    if (err != ESP_OK) {
        xSemaphoreGive(s_done);
    }
    return err;
}
