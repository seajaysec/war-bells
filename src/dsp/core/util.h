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

/* master "tape ceiling" — a warm program-dependent limiter, the cinematic way to handle energy
 * above the rails. Below threshold it's transparent. As it pushes, a peak envelope (fast attack,
 * slow release) rides the gain down (so washes BREATHE instead of buzzing), the highs gently roll
 * off (warmer/darker the harder it works — tape/optical character, never brighter), and a soft
 * asymptote guarantees it can never hard-clip. Per-channel state. */
typedef struct { float env, lp; } wb_tlim_t;
static inline float wb_tape_limit(wb_tlim_t *t, float x) {
    float a = x < 0.0f ? -x : x;
    t->env += (a - t->env) * (a > t->env ? 0.05f : 0.00015f);   /* fast attack ~0.5ms, slow release */
    /* clean gain-riding holds the level near the threshold (no waveshaping = no added harmonics) */
    float g = 1.0f;
    if (t->env > 0.6f) g = (0.6f + (t->env - 0.6f) * 0.18f) / t->env;   /* firm limit toward ~0.6-0.8 */
    float y = x * g;
    /* WARMTH: roll highs off by how hard you're PUSHING (envelope over threshold), not just by the
     * gain reduction — so even when the gain ride holds the level, hot swells audibly soften/darken
     * (tape/optical HF loss). Done after the gain ride, so it darkens rather than brightens. */
    float over = t->env - 0.6f; if (over < 0.0f) over = 0.0f;
    float work = over / (over + 0.5f);                           /* 0 (clean) .. ~1 (pushed hard) */
    float coef = 1.0f - work * 1.6f; if (coef < 0.1f) coef = 0.1f;
    t->lp += (y - t->lp) * coef;
    float blend = work * 0.8f;
    y += (t->lp - y) * blend;
    /* final soft ceiling — only catches the rare overshoot the gain ride didn't (minimal distortion) */
    float s = y < 0.0f ? -1.0f : 1.0f, ay = y < 0.0f ? -y : y;
    if (ay > 0.9f) ay = 0.9f + 0.1f * ((ay - 0.9f) / ((ay - 0.9f) + 0.15f));
    return s * ay;
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

/* shared Hann window LUT (power-of-two) — grain envelopes hit this per sample per grain,
 * so a table lookup instead of cosf() is real CPU headroom at high grain density. */
#define WB_HANN_N 2048
static float WB_HANN[WB_HANN_N];
static int   WB_HANN_READY = 0;
static inline void wb_hann_init(void) {
    if (WB_HANN_READY) return;
    for (int i = 0; i < WB_HANN_N; i++)
        WB_HANN[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)WB_HANN_N);
    WB_HANN_READY = 1;
}
static inline float wb_hann(float ph01) {   /* ph01 in [0,1) */
    if (!WB_HANN_READY) wb_hann_init();   /* per-TU table: ensure init in every translation unit */
    int i = (int)(ph01 * WB_HANN_N) & (WB_HANN_N - 1);
    return WB_HANN[i];
}

/* shared tan-coefficient LUT for the SVF: g = tan(pi*cutoff/SR). Filter sweeps / bandpass /
 * Motion recompute coeffs PER SAMPLE; this turns that tanf into a lerp'd table read (no audible
 * change — interpolated, ~5 Hz grid). Linear-indexed so there's no logf per lookup either. */
#define WB_TANG_N 4096
static float WB_TANG[WB_TANG_N];
static int   WB_TANG_READY = 0;
static inline void wb_tang_init(void) {
    if (WB_TANG_READY) return;
    for (int i = 0; i < WB_TANG_N; i++) {
        float fc = 20.0f + (20000.0f - 20.0f) * (float)i / (float)(WB_TANG_N - 1);
        WB_TANG[i] = tanf((float)M_PI * fc / WB_SR);
    }
    WB_TANG_READY = 1;
}
static inline float wb_tan_g(float cutoff) {
    if (!WB_TANG_READY) wb_tang_init();   /* per-TU table: ensure init in every translation unit */
    if (cutoff < 20.0f) cutoff = 20.0f; else if (cutoff > 20000.0f) cutoff = 20000.0f;
    float pos = (cutoff - 20.0f) * (float)(WB_TANG_N - 1) / (20000.0f - 20.0f);
    int i = (int)pos; if (i >= WB_TANG_N - 1) i = WB_TANG_N - 2;
    float f = pos - (float)i;
    return WB_TANG[i] + (WB_TANG[i + 1] - WB_TANG[i]) * f;
}

/* shared sine LUT for the LFOs (chorus / grain & multitap sweep / Motion) — these are sinf
 * per sample (per voice for sweeps); a lerp'd table read is inaudible. ph in turns (cycles). */
#define WB_SIN_N 2048
static float WB_SIN[WB_SIN_N];
static int   WB_SIN_READY = 0;
static inline void wb_sin_init(void) {
    if (WB_SIN_READY) return;
    for (int i = 0; i < WB_SIN_N; i++) WB_SIN[i] = sinf(2.0f * (float)M_PI * (float)i / (float)WB_SIN_N);
    WB_SIN_READY = 1;
}
static inline float wb_sin_turns(float ph) {     /* sin(2*pi*ph), ph in cycles */
    if (!WB_SIN_READY) wb_sin_init();   /* per-TU table: ensure init in every translation unit */
    ph -= (float)(int)ph; if (ph < 0.0f) ph += 1.0f;
    float x = ph * (float)WB_SIN_N;
    int i = (int)x; float f = x - (float)i;
    int j = (i + 1) & (WB_SIN_N - 1); i &= (WB_SIN_N - 1);
    return WB_SIN[i] + (WB_SIN[j] - WB_SIN[i]) * f;
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
