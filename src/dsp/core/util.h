/* util.h — small realtime-safe helpers shared by War Bells DSP primitives. */
#ifndef WB_UTIL_H
#define WB_UTIL_H

#include <stdint.h>
#include <math.h>

#define WB_SR        44100.0f
#define WB_FRAMES    128

/* per-voice / per-tap filter character */
enum { WB_F_NONE = 0, WB_F_SWEEP = 1, WB_F_BANDPASS = 2, WB_F_RANDOM = 3 };

static inline float wb_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static inline float wb_lerpf(float a, float b, float t) { return a + (b - a) * t; }

static inline float wb_i16_to_f(int16_t s) { return (float)s * (1.0f / 32768.0f); }

static inline int16_t wb_f_to_i16(float x) {
    x *= 32767.0f;
    if (x > 32767.0f) x = 32767.0f;
    if (x < -32768.0f) x = -32768.0f;
    return (int16_t)x;
}

/* gentle saturation to keep summed wet signals in range */
static inline float wb_softclip(float x) {
    if (x > 1.2f) x = 1.2f;
    if (x < -1.2f) x = -1.2f;
    return x - (0.148148f * x * x * x); /* x - x^3 * (1/6.75) */
}

/* bit-crush a -1..1 sample to `bits` bit depth (>=24 ~ transparent) */
static inline float wb_crush(float x, float bits) {
    if (bits >= 23.5f) return x;
    float levels = powf(2.0f, bits);
    return floorf(x * levels + 0.5f) / levels;
}

/* snap a positive playback/pitch ratio to the nearest pitch in a musical scale,
 * preserving octave register. scale: 0 Off, 1 Maj, 2 Min, 3 Pent, 4 Oct, 5 5th.
 * Returns ratio unchanged when scale<=0 (so Off == bit-identical to before). */
static inline float wb_scale_snap(float ratio, int scale) {
    if (scale <= 0 || ratio <= 1e-4f) return ratio;
    static const int SETS[6][13] = {
        {0,-1},                          /* 0 off (unused) */
        {0,2,4,5,7,9,11,-1},             /* Maj */
        {0,2,3,5,7,8,10,-1},             /* Min */
        {0,2,4,7,9,-1},                  /* Pent */
        {0,-1},                          /* Oct */
        {0,7,-1},                        /* 5th */
    };
    const int *set = SETS[(scale > 0 && scale < 6) ? scale : 1];
    float semis  = 12.0f * log2f(ratio);
    float oct    = floorf(semis / 12.0f);
    float within = semis - oct * 12.0f;          /* 0..12 */
    float best = 0.0f, bestd = 1e9f;
    for (int i = 0; set[i] >= 0; i++) {
        float d = within - (float)set[i]; if (d < 0) d = -d;
        if (d < bestd) { bestd = d; best = (float)set[i]; }
    }
    if (12.0f - within < bestd) best = 12.0f;    /* wrap to next octave root */
    return powf(2.0f, (oct * 12.0f + best) / 12.0f);
}

/* xorshift32 — cheap deterministic RNG for grain jitter/scatter */
static inline uint32_t wb_rng_next(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}
/* uniform float in [-1, 1] */
static inline float wb_rng_bi(uint32_t *s) {
    return (float)(wb_rng_next(s) >> 9) * (1.0f / 4194304.0f) - 1.0f;
}
/* uniform float in [0, 1] */
static inline float wb_rng_uni(uint32_t *s) {
    return (float)(wb_rng_next(s) >> 8) * (1.0f / 16777216.0f);
}

#endif /* WB_UTIL_H */
