/* grain.h — one granular looper voice (the GrainBuf analogue).
 *
 * Reads the most-recent `window` samples of the capture ring and plays short
 * Hann-windowed grains from a sweeping playhead. `rate` ties pitch + traversal
 * (a micro-loop sped up = octave up), so speed sets {0.5,1,2,4}. Per-voice
 * character: scatter (rearrangement), spread (stereo), filter mode, bit-crush,
 * and a triangle glide between two rates. One svf per voice for tone. */
#ifndef WB_GRAIN_H
#define WB_GRAIN_H

#include "util.h"
#include "ringbuf.h"
#include "svf.h"

#define WB_GRAINS_PER_VOICE 8
/* WB_F_* filter-mode enum lives in util.h (shared with multitap.h) */

typedef struct {
    int   active;
    float basepos;   /* absolute ring index where the grain started */
    float age;       /* samples since spawn */
    float dur;       /* grain length in samples */
    float rate;      /* playback rate at spawn */
    float gl, gr;    /* per-grain L/R pan gains */
    float cut;       /* per-grain cutoff (for WB_F_RANDOM) */
} wb_grain_t;

typedef struct {
    wb_grain_t g[WB_GRAINS_PER_VOICE];
    double sched;        /* grain scheduler phase accumulator */
    double ph;           /* playhead within [0, window) */
    /* live, smoothed params */
    float window;        /* loop length in samples */
    float rate, size, density, scatter, spread, gain;
    float cutoff, rq, crush;
    int   fmode;
    int   envmode;       /* grain amplitude env: 0 Soft(Hann) 1 Pluck(down) 2 Swell(up) 3 Gate */
    int   scale;         /* 0 off, else snap playback rate to a musical scale (wb_scale_snap) */
    /* glide */
    int   glide, bidir;
    float glo, ghi, gfreq;
    double glide_ph;
    /* smoothing + state */
    float gain_cur, rate_cur;
    float sweep_ph;      /* slow LFO phase for sweep filter */
    wb_svf_t filt;
    uint32_t rng;
} wb_voice_t;

static inline void wb_voice_init(wb_voice_t *v, uint32_t seed) {
    wb_hann_init();
    for (int i = 0; i < WB_GRAINS_PER_VOICE; i++) v->g[i].active = 0;
    v->sched = 0.0; v->ph = 0.0; v->glide_ph = 0.0; v->sweep_ph = 0.0;
    v->window = WB_SR * 0.5f;
    v->rate = 1.0f; v->size = 0.14f; v->density = 12.0f;
    v->scatter = 0.1f; v->spread = 0.2f; v->gain = 0.0f;
    v->cutoff = 16000.0f; v->rq = 0.4f; v->crush = 24.0f;
    v->fmode = WB_F_NONE; v->envmode = 0; v->scale = 0; v->glide = 0; v->bidir = 0;
    v->glo = 1.0f; v->ghi = 1.0f; v->gfreq = 0.3f;
    v->gain_cur = 0.0f; v->rate_cur = 1.0f;
    v->rng = seed ? seed : 0x1234567u;
    wb_svf_reset(&v->filt);
}

/* effective playback rate this sample (triangle glide if enabled, scale-snapped if set) */
static inline float wb_voice_eff_rate(wb_voice_t *v) {
    float r;
    if (!v->glide) r = v->rate;
    else {
        /* triangle 0..1..0 */
        double p = v->glide_ph + (v->bidir ? 0.5 : 0.0);
        if (p >= 1.0) p -= 1.0;
        float tri = (p < 0.5) ? (float)(p * 2.0) : (float)(2.0 - p * 2.0);
        v->glide_ph += (double)(v->gfreq / WB_SR);
        if (v->glide_ph >= 1.0) v->glide_ph -= 1.0;
        r = wb_lerpf(v->glo, v->ghi, tri);
    }
    if (v->scale) {                              /* quantize |rate| to the scale, keep sign */
        float s = (r < 0.0f) ? -1.0f : 1.0f;
        r = s * wb_scale_snap(r < 0.0f ? -r : r, v->scale);
    }
    return r;
}

static inline void wb_voice_spawn(wb_voice_t *v, const wb_ring_t *rb, float eff_rate) {
    int slot = -1;
    for (int i = 0; i < WB_GRAINS_PER_VOICE; i++) if (!v->g[i].active) { slot = i; break; }
    if (slot < 0) return; /* pool full — drop (keeps CPU bounded) */
    wb_grain_t *g = &v->g[slot];
    float off = (float)v->ph + v->scatter * v->window * wb_rng_bi(&v->rng);
    g->basepos = (float)rb->w - v->window + off;
    g->age = 0.0f;
    g->dur = wb_clampf(v->size * WB_SR, 32.0f, v->window * 2.0f + 64.0f);
    g->rate = eff_rate;
    float pan = wb_clampf(v->spread * wb_rng_bi(&v->rng), -1.0f, 1.0f);
    g->gl = 0.5f * (1.0f - pan);
    g->gr = 0.5f * (1.0f + pan);
    g->cut = (v->fmode == WB_F_RANDOM)
        ? (400.0f + wb_rng_uni(&v->rng) * (v->cutoff - 400.0f)) : v->cutoff;
    g->active = 1;
}

/* render one stereo sample of this voice; accumulates into outL,outR */
static inline void wb_voice_process(wb_voice_t *v, const wb_ring_t *rb,
                                    float *outL, float *outR) {
    /* param smoothing */
    v->gain_cur += (v->gain - v->gain_cur) * 0.002f;
    if (v->gain_cur < 1e-5f && v->gain < 1e-5f) {
        /* idle: still advance playhead so it stays in sync, but skip work */
    }
    float eff = wb_voice_eff_rate(v);
    v->rate_cur += (eff - v->rate_cur) * 0.01f;

    /* schedule grains */
    v->sched += (double)(v->density / WB_SR);
    if (v->sched >= 1.0) {
        v->sched -= 1.0;
        if (v->gain_cur > 1e-4f || v->gain > 1e-4f)
            wb_voice_spawn(v, rb, v->rate_cur);
    }
    /* advance playhead within the loop window */
    v->ph += (double)v->rate_cur;
    if (v->window > 1.0f) {
        while (v->ph >= (double)v->window) v->ph -= (double)v->window;
        while (v->ph < 0.0) v->ph += (double)v->window;
    }

    /* sum active grains */
    float sumL = 0.0f, sumR = 0.0f;
    float sweepCut = v->cutoff;
    if (v->fmode == WB_F_SWEEP) {
        v->sweep_ph += (double)(0.3f / WB_SR);
        if (v->sweep_ph >= 1.0) v->sweep_ph -= 1.0;
        float lfo = 0.5f + 0.5f * sinf((float)(2.0 * M_PI * v->sweep_ph));
        sweepCut = wb_lerpf(v->cutoff * 0.15f, v->cutoff, lfo);
    }
    for (int i = 0; i < WB_GRAINS_PER_VOICE; i++) {
        wb_grain_t *g = &v->g[i];
        if (!g->active) continue;
        float w = g->age / g->dur;
        if (w >= 1.0f) { g->active = 0; continue; }
        float win;                                   /* grain amplitude envelope */
        switch (v->envmode) {
            case 1: win = 1.0f - w; break;           /* Pluck: abrupt onset, fade out */
            case 2: win = w; break;                  /* Swell: ramp up (reverse feel) */
            case 3: { float e = 0.05f;               /* Gate: flat with short edge fades */
                      win = (w < e) ? (w / e) : (w > 1.0f - e ? (1.0f - w) / e : 1.0f); break; }
            default: win = wb_hann(w); break;                                  /* Soft (Hann, LUT) */
        }
        float sl, sr;
        wb_ring_read(rb, g->basepos + g->age * g->rate, &sl, &sr);
        sumL += sl * win * g->gl;
        sumR += sr * win * g->gr;
        g->age += 1.0f;
    }

    /* per-voice filter character */
    if (v->fmode != WB_F_NONE) {
        float fc = (v->fmode == WB_F_SWEEP) ? sweepCut : v->cutoff;
        if (v->fmode == WB_F_BANDPASS) {
            wb_svf_set(&v->filt, fc, v->rq);
            sumL = wb_svf_bp(&v->filt, sumL);
            sumR = sumL; /* bp uses one state; approximate mono-ish bp */
        } else {
            wb_svf_set(&v->filt, fc, v->rq);
            sumL = wb_svf_lp(&v->filt, sumL);
            /* keep stereo: reuse coeffs on R via a second cheap pass */
            sumR = wb_svf_lp(&v->filt, sumR);
        }
    }
    if (v->crush < 23.5f) {
        sumL = wb_crush(sumL, v->crush);
        sumR = wb_crush(sumR, v->crush);
    }

    *outL += sumL * v->gain_cur;
    *outR += sumR * v->gain_cur;
}

#endif /* WB_GRAIN_H */
