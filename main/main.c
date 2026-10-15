// SPDX-License-Identifier: MIT
#include <math.h>
#include <stdbool.h>

#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sandsim.h"

static const char *TAG = "hourglass";

#define FPS 30
#define FLIP_STILL_FRAMES (FPS * 3 / 2) // empty top and no movement for 1.5 s
#define FLIP_FRAMES (FPS * 6 / 5)       // the flip animation takes 1.2 s

static const float PI = 3.14159265f;

typedef struct {
    float cos_a, sin_a, scale;
} rotation_t;

static void plot(int x, int y, void *ctx)
{
    display_pixel(x, y);
}

// Rotate around the screen centre and shrink so the turning glass stays on screen.
static void plot_rotated(int x, int y, void *ctx)
{
    const rotation_t *r = ctx;
    float cx = DISPLAY_WIDTH / 2.0f;
    float cy = DISPLAY_HEIGHT / 2.0f;
    float dx = (x + 0.5f - cx) * r->scale;
    float dy = (y + 0.5f - cy) * r->scale;
    display_pixel((int)floorf(cx + dx * r->cos_a - dy * r->sin_a),
                  (int)floorf(cy + dx * r->sin_a + dy * r->cos_a));
}

static void draw_flip(const sandsim_t *sim, int frame)
{
    float t = (float)frame / FLIP_FRAMES;
    float angle = PI * (1.0f - cosf(PI * t)) / 2.0f; // ease in and out
    rotation_t r = {cosf(angle), sinf(angle), 1.0f};
    float ac = fabsf(r.cos_a);
    float as = fabsf(r.sin_a);
    float fit_w = DISPLAY_WIDTH / (DISPLAY_WIDTH * ac + DISPLAY_HEIGHT * as);
    float fit_h = DISPLAY_HEIGHT / (DISPLAY_WIDTH * as + DISPLAY_HEIGHT * ac);
    r.scale = fminf(1.0f, fminf(fit_w, fit_h));

    sandsim_draw_glass(sim, plot_rotated, &r);
    sandsim_draw_grains(sim, plot_rotated, &r);
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_init());

    sandsim_params_t params;
    sandsim_default_params(&params);
    params.width = DISPLAY_WIDTH;
    params.height = DISPLAY_HEIGHT;
    sandsim_t *sim = sandsim_create(&params);
    if (!sim) {
        ESP_LOGE(TAG, "could not create the simulation");
        return;
    }
    ESP_LOGI(TAG, "%d grains, neck %d px", params.grains, params.neck_width);

    int still = 0;
    int flipping = -1; // frame of the flip animation, -1 when the sand is running
    int frames = 0;
    int64_t step_us = 0;
    int64_t run_start = esp_timer_get_time();
    int64_t log_start = run_start;
    TickType_t wake = xTaskGetTickCount();

    while (true) {
        display_clear();
        if (flipping < 0) {
            int64_t t0 = esp_timer_get_time();
            sandsim_step(sim, 1.0f / FPS);
            step_us += esp_timer_get_time() - t0;

            bool settled = sandsim_grains_on_top(sim) == 0 && sandsim_grains_moving(sim) == 0;
            still = settled ? still + 1 : 0;
            if (still >= FLIP_STILL_FRAMES) {
                ESP_LOGI(TAG, "sand ran out after %.1f s, flipping",
                         (esp_timer_get_time() - run_start) / 1e6);
                flipping = 0;
                still = 0;
            }
            sandsim_draw_glass(sim, plot, NULL);
            sandsim_draw_grains(sim, plot, NULL);
        } else {
            draw_flip(sim, flipping);
            if (++flipping > FLIP_FRAMES) {
                sandsim_flip(sim);
                flipping = -1;
                run_start = esp_timer_get_time();
            }
        }
        ESP_ERROR_CHECK(display_flush());

        frames++;
        int64_t now = esp_timer_get_time();
        if (now - log_start >= 5000000) {
            ESP_LOGI(TAG, "%.1f fps, physics %lld us/frame, %d on top, %d moving",
                     frames * 1e6 / (double)(now - log_start), step_us / frames,
                     sandsim_grains_on_top(sim), sandsim_grains_moving(sim));
            frames = 0;
            step_us = 0;
            log_start = now;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000 / FPS));
    }
}
