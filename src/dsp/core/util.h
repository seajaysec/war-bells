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
