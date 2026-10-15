// SPDX-License-Identifier: MIT
#include <stdbool.h>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "hourglass";

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "Hourglass starting: %d core(s), rev %d", chip.cores, chip.revision);
    ESP_LOGI(TAG, "PSRAM free: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "LCD pins: CS=%d MOSI=%d SCK=%d DC=%d RST=%d @ %d MHz",
             CONFIG_HOURGLASS_LCD_PIN_CS, CONFIG_HOURGLASS_LCD_PIN_MOSI,
             CONFIG_HOURGLASS_LCD_PIN_SCK, CONFIG_HOURGLASS_LCD_PIN_DC,
             CONFIG_HOURGLASS_LCD_PIN_RST, CONFIG_HOURGLASS_LCD_PIXEL_CLOCK_MHZ);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
