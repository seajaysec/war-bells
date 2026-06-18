/* chorus.h — pitch modulation (subtle chorus -> detuned wobble) via a short
 * LFO-modulated delay. depth 0..1, rate in Hz. Stereo with quadrature LFOs. */
#ifndef WB_CHORUS_H
#define WB_CHORUS_H

#include <math.h>
#include "util.h"

#define WB_CH_LEN 2048   /* ~46 ms */

typedef struct {
    float l[WB_CH_LEN], r[WB_CH_LEN];
    int   w;
    float ph;
    float depth, rate;
} wb_chorus_t;

static inline void wb_chorus_init(wb_chorus_t *c) {
    for (int i = 0; i < WB_CH_LEN; i++) { c->l[i] = 0.0f; c->r[i] = 0.0f; }
    c->w = 0; c->ph = 0.0f; c->depth = 0.0f; c->rate = 4.0f;
}

static inline float wb_ch_read(const float *b, int w, float dly) {
    float pos = (float)w - dly;
    while (pos < 0.0f) pos += WB_CH_LEN;
    int i0 = (int)pos; int i1 = i0 + 1; if (i1 >= WB_CH_LEN) i1 = 0;
    float f = pos - (float)i0;
    return wb_lerpf(b[i0], b[i1], f);
}

static inline void wb_chorus_process(wb_chorus_t *c, float inL, float inR,
                                     float *outL, float *outR) {
    c->l[c->w] = inL; c->r[c->w] = inR;
    if (c->depth <= 1e-4f) {
        *outL = inL; *outR = inR;
        if (++c->w >= WB_CH_LEN) c->w = 0;
        return;
    }
    float base = 0.0008f * WB_SR;                 /* 0.8 ms */
    float range = (0.012f * c->depth) * WB_SR;    /* up to 12 ms */
    float lfoL = 0.5f + 0.5f * sinf((float)(2.0 * M_PI) * c->ph);
    float lfoR = 0.5f + 0.5f * sinf((float)(2.0 * M_PI) * c->ph + 1.5708f);
    float wetL = wb_ch_read(c->l, c->w, base + range * lfoL);
    float wetR = wb_ch_read(c->r, c->w, base + range * lfoR);
    c->ph += c->rate / WB_SR; if (c->ph >= 1.0f) c->ph -= 1.0f;
    if (++c->w >= WB_CH_LEN) c->w = 0;

    float mix = c->depth * 0.6f;
    *outL = wb_lerpf(inL, wetL, mix);
    *outR = wb_lerpf(inR, wetR, mix);
}

#endif /* WB_CHORUS_H */
