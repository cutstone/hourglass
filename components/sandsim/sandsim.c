// SPDX-License-Identifier: MIT
#include "sandsim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"

// The simulation touches every grain many times per frame: keep it out of the slower PSRAM.
static void *alloc_fast(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p ? p : malloc(size);
}
#else
#define alloc_fast malloc
#endif

enum { CELL_EMPTY = 0, CELL_WALL = 1, CELL_GRAIN = 2 };

#define HALF_PI 1.57079633f

typedef struct {
    float x, y;   // position in pixels; the grain occupies cell (floor(x), floor(y))
    float vx, vy; // velocity in pixels / s
    bool asleep;  // resting with no way to move; skipped until room opens up
} grain_t;

struct sandsim {
    sandsim_params_t p;
    uint8_t *cells;  // width * height
    grain_t *grains; // sorted bottom-up before every step
    uint16_t *awake; // indices of the grains that are not asleep
    int awake_len;
    int16_t *outline; // x, y pairs of the glass outline
    int outline_len;
    int moving;
    uint32_t rng;
};

void sandsim_default_params(sandsim_params_t *p)
{
    *p = (sandsim_params_t){
        .width = 168,
        .height = 384,
        .grains = 7500,
        .glass_size = 0.9f,
        .glass_line = 2,
        .neck_width = 4,
        .gravity = 600.0f,
        .max_speed = 240.0f,
        .elasticity = 0.15f,
        .friction = 0.6f,
    };
}

static uint32_t rnd(sandsim_t *s)
{
    // xorshift32
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s->rng = x;
}

static inline uint8_t *cell(sandsim_t *s, int x, int y)
{
    return &s->cells[y * s->p.width + x];
}

static inline bool is_free(sandsim_t *s, int x, int y)
{
    if (x < 0 || y < 0 || x >= s->p.width || y >= s->p.height) {
        return false;
    }
    return *cell(s, x, y) == CELL_EMPTY;
}

// Half width of the glass interior at row y (negative: closed).
static float glass_half_width(const sandsim_params_t *p, int y)
{
    const float margin = 3.0f;
    float corner = 14.0f * p->glass_size; // radius of the rounded bulb ends
    float max_half = (p->width / 2.0f - margin) * p->glass_size;
    float half_height = (p->height / 2.0f - margin) * p->glass_size;
    float neck = p->neck_width / 2.0f;

    float dist = fabsf(y + 0.5f - p->height / 2.0f); // from the neck
    float d = dist / half_height;
    if (d >= 1.0f) {
        return -1.0f;
    }
    // Narrow neck that opens up into a wide bulb.
    float t = fminf(1.0f, d / 0.55f);
    float shape = sinf(t * HALF_PI);
    float half = neck + (max_half - neck) * shape * sqrtf(shape);

    // Flat ends with rounded corners.
    float from_end = half_height - dist;
    if (from_end < corner) {
        float k = corner - from_end;
        half = fminf(half, max_half - corner + sqrtf(corner * corner - k * k));
    }
    return half;
}

static bool build_glass(sandsim_t *s)
{
    const int w = s->p.width;
    const int h = s->p.height;
    float cx = w / 2.0f;
    for (int y = 0; y < h; y++) {
        float half = glass_half_width(&s->p, y);
        for (int x = 0; x < w; x++) {
            bool inside = half > 0 && fabsf(x + 0.5f - cx) < half + 0.01f;
            *cell(s, x, y) = inside ? CELL_EMPTY : CELL_WALL;
        }
    }

    // Outline: wall pixels within glass_line pixels of the interior. Around the neck the
    // line stays 1 pixel, so the narrow middle keeps its shape.
    const int neck_zone = h / 8;
    int count = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            int r = abs(y - h / 2) < neck_zone ? 1 : s->p.glass_line;
            for (int x = 0; x < w; x++) {
                if (*cell(s, x, y) != CELL_WALL) {
                    continue;
                }
                bool edge = false;
                for (int dy = -r; dy <= r && !edge; dy++) {
                    for (int dx = -r; dx <= r && !edge; dx++) {
                        edge = is_free(s, x + dx, y + dy);
                    }
                }
                if (edge) {
                    if (pass == 1) {
                        s->outline[2 * count] = (int16_t)x;
                        s->outline[2 * count + 1] = (int16_t)y;
                    }
                    count++;
                }
            }
        }
        if (pass == 0) {
            s->outline = malloc(sizeof(int16_t) * 2 * count);
            if (!s->outline) {
                return false;
            }
            s->outline_len = count;
            count = 0;
        }
    }
    return true;
}

// Fill the top bulb from the neck upwards.
static bool fill_top(sandsim_t *s)
{
    int n = 0;
    for (int y = s->p.height / 2 - 1; y >= 0 && n < s->p.grains; y--) {
        for (int x = 0; x < s->p.width && n < s->p.grains; x++) {
            if (*cell(s, x, y) == CELL_EMPTY) {
                *cell(s, x, y) = CELL_GRAIN;
                s->grains[n++] = (grain_t){x + 0.5f, y + 0.5f, 0, 0, false};
            }
        }
    }
    return n == s->p.grains;
}

sandsim_t *sandsim_create(const sandsim_params_t *p)
{
    sandsim_t *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->p = *p;
    s->rng = 0x9E3779B9u;
    if (p->grains > UINT16_MAX) {
        free(s);
        return NULL;
    }
    s->cells = alloc_fast((size_t)p->width * p->height);
    s->grains = alloc_fast(sizeof(grain_t) * p->grains);
    s->awake = alloc_fast(sizeof(uint16_t) * p->grains);
    if (!s->cells || !s->grains || !s->awake || !build_glass(s) || !fill_top(s)) {
        sandsim_destroy(s);
        return NULL;
    }
    return s;
}

void sandsim_destroy(sandsim_t *s)
{
    if (s) {
        free(s->cells);
        free(s->grains);
        free(s->awake);
        free(s->outline);
        free(s);
    }
}

// Insertion sort, bottom (largest y) first. The order barely changes between steps,
// so this is close to linear. Moving the lowest grains first lets columns fall freely.
static void sort_bottom_up(sandsim_t *s)
{
    grain_t *g = s->grains;
    for (int i = 1; i < s->p.grains; i++) {
        grain_t key = g[i];
        int j = i - 1;
        while (j >= 0 && g[j].y < key.y) {
            g[j + 1] = g[j];
            j--;
        }
        g[j + 1] = key;
    }
}

static inline void move_grain(sandsim_t *s, int fx, int fy, int tx, int ty)
{
    *cell(s, fx, fy) = CELL_EMPTY;
    *cell(s, tx, ty) = CELL_GRAIN;
}

// Where a grain resting at (cx, cy) can slide to, if anywhere:
// - diagonally down, so heaps of sand form 45 degree slopes;
// - sideways along the glass, which is smooth, so grains do not stay on walls flatter than 45.
static bool find_slide(sandsim_t *s, int cx, int cy, int dir, int *tx, int *ty)
{
    for (int i = 0; i < 2; i++, dir = -dir) {
        if (is_free(s, cx + dir, cy + 1) && is_free(s, cx + dir, cy)) {
            *tx = cx + dir;
            *ty = cy + 1;
            return true;
        }
    }
    if (cy + 1 < s->p.height && *cell(s, cx, cy + 1) == CELL_WALL) {
        for (int i = 0; i < 2; i++, dir = -dir) {
            if (is_free(s, cx + dir, cy) && is_free(s, cx + 2 * dir, cy + 1)) {
                *tx = cx + dir;
                *ty = cy;
                return true;
            }
        }
    }
    return false;
}

static void step_grain(sandsim_t *s, grain_t *g, float h)
{
    const sandsim_params_t *p = &s->p;
    int cx = (int)g->x;
    int cy = (int)g->y;

    g->vy += p->gravity * h;
    float speed2 = g->vx * g->vx + g->vy * g->vy;
    if (speed2 > p->max_speed * p->max_speed) {
        float k = p->max_speed / sqrtf(speed2);
        g->vx *= k;
        g->vy *= k;
    }

    float nx = g->x + g->vx * h;
    float ny = g->y + g->vy * h;
    int tx = (int)floorf(nx);
    int ty = (int)floorf(ny);

    if (tx == cx && ty == cy) {
        g->x = nx;
        g->y = ny;
        return;
    }
    if (is_free(s, tx, ty)) {
        move_grain(s, cx, cy, tx, ty);
        g->x = nx;
        g->y = ny;
        s->moving++;
        return;
    }

    // Blocked. Try the horizontal and vertical part of the move separately.
    if (tx != cx && ty != cy) {
        if (is_free(s, cx, ty)) {
            move_grain(s, cx, cy, cx, ty);
            g->y = ny;
            g->x = cx + (g->vx > 0 ? 0.999f : 0.0f);
            g->vx = -g->vx * p->elasticity;
            s->moving++;
            return;
        }
        if (is_free(s, tx, cy)) {
            move_grain(s, cx, cy, tx, cy);
            g->x = nx;
            g->y = cy + (g->vy > 0 ? 0.999f : 0.0f);
            g->vy = -g->vy * p->elasticity;
            g->vx *= p->friction;
            s->moving++;
            return;
        }
    }

    if (ty == cy) {
        // Hit something sideways.
        g->x = cx + (tx > cx ? 0.999f : 0.0f);
        g->vx = -g->vx * p->elasticity;
        return;
    }

    // Landed on (or hit) something. Bounce a little and lose sideways speed.
    bool falling = ty > cy;
    g->y = cy + (falling ? 0.999f : 0.0f);
    g->x = fminf(fmaxf(nx, cx), cx + 0.999f);
    g->vy = -g->vy * p->elasticity;
    g->vx *= p->friction;
    if (!falling) {
        return;
    }

    int dir = g->vx > 1.0f ? 1 : g->vx < -1.0f ? -1 : (rnd(s) & 1) ? 1 : -1;
    int sx, sy;
    if (find_slide(s, cx, cy, dir, &sx, &sy)) {
        move_grain(s, cx, cy, sx, sy);
        g->x = sx + 0.5f;
        g->y = sy + 0.5f;
        g->vx = (sx - cx) * fmaxf(fabsf(g->vy), 20.0f);
        s->moving++;
    } else if (fabsf(g->vx) < 2.0f) {
        g->asleep = true;
        g->vx = 0;
        g->vy = 0;
    }
}

void sandsim_step(sandsim_t *s, float dt)
{
    // Substeps keep every move within one pixel.
    int steps = (int)ceilf(s->p.max_speed * dt);
    if (steps < 1) {
        steps = 1;
    }
    float h = dt / steps;

    s->moving = 0;
    sort_bottom_up(s);

    // Wake sleeping grains that got room below them, and list the ones to simulate.
    s->awake_len = 0;
    for (int i = 0; i < s->p.grains; i++) {
        grain_t *g = &s->grains[i];
        if (g->asleep) {
            int cx = (int)g->x;
            int cy = (int)g->y;
            int tx, ty;
            if (!is_free(s, cx, cy + 1) && !find_slide(s, cx, cy, 1, &tx, &ty)) {
                continue;
            }
            g->asleep = false;
        }
        s->awake[s->awake_len++] = (uint16_t)i;
    }

    for (int k = 0; k < steps; k++) {
        for (int i = 0; i < s->awake_len; i++) {
            grain_t *g = &s->grains[s->awake[i]];
            if (!g->asleep) {
                step_grain(s, g, h);
            }
        }
    }
}

void sandsim_flip(sandsim_t *s)
{
    const int w = s->p.width;
    const int h = s->p.height;
    for (int i = 0; i < s->p.grains; i++) {
        grain_t *g = &s->grains[i];
        *cell(s, (int)g->x, (int)g->y) = CELL_EMPTY;
    }
    // The glass is symmetric, so only the grains move.
    for (int i = 0; i < s->p.grains; i++) {
        grain_t *g = &s->grains[i];
        int x = w - 1 - (int)g->x;
        int y = h - 1 - (int)g->y;
        *g = (grain_t){x + 0.5f, y + 0.5f, 0, 0, false};
        *cell(s, x, y) = CELL_GRAIN;
    }
    // Mirroring reversed the bottom-up order; restore it so the sort stays cheap.
    for (int i = 0, j = s->p.grains - 1; i < j; i++, j--) {
        grain_t t = s->grains[i];
        s->grains[i] = s->grains[j];
        s->grains[j] = t;
    }
}

int sandsim_grain_count(const sandsim_t *s)
{
    return s->p.grains;
}

int sandsim_grains_on_top(const sandsim_t *s)
{
    int n = 0;
    float mid = s->p.height / 2.0f;
    for (int i = 0; i < s->p.grains; i++) {
        n += s->grains[i].y < mid;
    }
    return n;
}

int sandsim_grains_moving(const sandsim_t *s)
{
    return s->moving;
}

void sandsim_draw_grains(const sandsim_t *s, sandsim_plot_fn plot, void *ctx)
{
    for (int i = 0; i < s->p.grains; i++) {
        plot((int)s->grains[i].x, (int)s->grains[i].y, ctx);
    }
}

void sandsim_draw_glass(const sandsim_t *s, sandsim_plot_fn plot, void *ctx)
{
    for (int i = 0; i < s->outline_len; i++) {
        plot(s->outline[2 * i], s->outline[2 * i + 1], ctx);
    }
}
