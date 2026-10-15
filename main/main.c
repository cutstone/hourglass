// SPDX-License-Identifier: MIT
#include <stdbool.h>

#include "display.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "hourglass";

// Test pattern: a frame, an hourglass outline, a solid marker in the top-left corner
// (shows the orientation) and a block sliding down (shows the refresh rate).
static void draw_test_pattern(int frame)
{
    const int w = DISPLAY_WIDTH;
    const int h = DISPLAY_HEIGHT;
    const int neck = 6;

    display_clear();
    display_rect(0, 0, w, h, 2);
    display_fill_rect(6, 6, 16, 16);

    display_line(20, 30, w - 21, 30);
    display_line(20, h - 31, w - 21, h - 31);
    display_line(20, 30, w / 2 - neck, h / 2);
    display_line(w - 21, 30, w / 2 + neck, h / 2);
    display_line(w / 2 - neck, h / 2, 20, h - 31);
    display_line(w / 2 + neck, h / 2, w - 21, h - 31);

    int y = 40 + (frame * 4) % (h - 90);
    display_fill_rect(w / 2 - 4, y, 8, 8);
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "Hourglass starting: %d core(s), rev %d", chip.cores, chip.revision);
    ESP_LOGI(TAG, "PSRAM free: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    ESP_ERROR_CHECK(display_init());

    int frames = 0;
    int64_t t0 = esp_timer_get_time();
    while (true) {
        draw_test_pattern(frames);
        ESP_ERROR_CHECK(display_flush());
        frames++;

        int64_t now = esp_timer_get_time();
        if (now - t0 >= 5000000) {
            ESP_LOGI(TAG, "%.1f fps", frames * 1e6 / (double)(now - t0));
            frames = 0;
            t0 = now;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
