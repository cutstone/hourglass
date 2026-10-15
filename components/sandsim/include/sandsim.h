// SPDX-License-Identifier: MIT
//
// Particle sand simulation inside an hourglass-shaped glass.
// Plain C without ESP-IDF dependencies, so it also builds and runs on a PC.
//
// Coordinates are in pixels: x to the right, y down, gravity points down (+y).
// Every grain is a particle with a position and a velocity, and occupies one pixel.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;        // simulation area in pixels (display size)
    int height;       //
    int grains;       // number of grains
    int neck_width;   // width of the opening between the bulbs, in pixels (even values)
    float gravity;    // pixels / s^2
    float max_speed;  // pixels / s
    float elasticity; // 0..1, share of the speed kept (reversed) when bouncing
    float friction;   // 0..1, share of the sideways speed kept on contact
} sandsim_params_t;

typedef struct sandsim sandsim_t;

// Called for every pixel to draw.
typedef void (*sandsim_plot_fn)(int x, int y, void *ctx);

void sandsim_default_params(sandsim_params_t *p);

// Builds the glass and fills the top bulb with grains resting on the neck.
// Returns NULL when out of memory or when the grains do not fit in one bulb.
sandsim_t *sandsim_create(const sandsim_params_t *p);
void sandsim_destroy(sandsim_t *sim);

// Advances the simulation by dt seconds.
void sandsim_step(sandsim_t *sim, float dt);

// Turns the glass upside down (rotates all grains 180 degrees around the centre).
void sandsim_flip(sandsim_t *sim);

int sandsim_grain_count(const sandsim_t *sim);
// Grains in the top bulb.
int sandsim_grains_on_top(const sandsim_t *sim);
// Grains that moved to another pixel during the last step.
int sandsim_grains_moving(const sandsim_t *sim);

void sandsim_draw_grains(const sandsim_t *sim, sandsim_plot_fn plot, void *ctx);
void sandsim_draw_glass(const sandsim_t *sim, sandsim_plot_fn plot, void *ctx);

#ifdef __cplusplus
}
#endif
