/* multitap.h — the MULTIDELAY engine (PATTERN / WARP).
 * Up to 4 taps with feedback over an engine-owned stereo delay line. Per-tap
 * (summed) filter character + optional pitch-shifted taps. The brain supplies
 * absolute tap times (seconds) and gains to realize the rhythmic patterns. */
#ifndef WB_MULTITAP_H
#define WB_MULTITAP_H

#include "util.h"
#include "svf.h"
#include "pitchshift.h"

typedef struct {
    float *l, *r;     /* engine-owned, length = len */
    int len;
    int w;
    /* params (seconds / linear) */
    float t[4], g[4];
    float fb;
    int   fmode;      /* WB_F_* (none/sweep->lp/bandpass) */
    float cutoff, rq;
    float pitch;      /* 1 = off */
    float level;      /* wet send into the bus (0 = delay path inactive) */
    /* state */
    wb_svf_t fl, fr;
    wb_pshift_t pl, pr;
    float sweep_ph;
} wb_delay_t;

static inline void wb_delay_init(wb_delay_t *d, float *l, float *r, int len) {
    d->l = l; d->r = r; d->len = len; d->w = 0;
    d->t[0]=0.3f; d->t[1]=0.6f; d->t[2]=0.9f; d->t[3]=1.2f;
    d->g[0]=1.0f; d->g[1]=0.7f; d->g[2]=0.5f; d->g[3]=0.3f;
    d->fb=0.35f; d->fmode=WB_F_NONE; d->cutoff=16000.0f; d->rq=0.4f;
    d->pitch=1.0f; d->level=0.0f; d->sweep_ph=0.0f;
    wb_svf_reset(&d->fl); wb_svf_reset(&d->fr);
    wb_pshift_init(&d->pl); wb_pshift_init(&d->pr);
}

static inline void wb_delay_readtap(const wb_delay_t *d, float tsec,
                                    float *ol, float *or_) {
    float dpos = (float)d->w - tsec * WB_SR;
    while (dpos < 0.0f) dpos += (float)d->len;
    while (dpos >= (float)d->len) dpos -= (float)d->len;
    int i0 = (int)dpos; int i1 = i0 + 1; if (i1 >= d->len) i1 = 0;
    float f = dpos - (float)i0;
    *ol = wb_lerpf(d->l[i0], d->l[i1], f);
    *or_ = wb_lerpf(d->r[i0], d->r[i1], f);
}

/* process one sample; returns wet (already scaled by level) in outL,outR */
static inline void wb_delay_process(wb_delay_t *d, float inL, float inR,
                                    float *outL, float *outR) {
    if (d->level <= 1e-5f) {
        /* keep the line primed (write dry) so re-enabling has continuity */
        d->l[d->w] = inL; d->r[d->w] = inR;
        if (++d->w >= d->len) d->w = 0;
        *outL = 0.0f; *outR = 0.0f;
        return;
    }
    float sL = 0.0f, sR = 0.0f, t1L, t1R, tl, tr;
    wb_delay_readtap(d, d->t[0], &t1L, &t1R);
    sL += t1L * d->g[0]; sR += t1R * d->g[0];
    for (int i = 1; i < 4; i++) {
        wb_delay_readtap(d, d->t[i], &tl, &tr);
        sL += tl * d->g[i]; sR += tr * d->g[i];
    }
    /* filter character */
    if (d->fmode == WB_F_SWEEP) {
        d->sweep_ph += 0.25f / WB_SR; if (d->sweep_ph >= 1.0f) d->sweep_ph -= 1.0f;
        float lfo = 0.5f + 0.5f * sinf((float)(2.0 * M_PI * d->sweep_ph));
        float fc = wb_lerpf(d->cutoff * 0.2f, d->cutoff, lfo);
        wb_svf_set(&d->fl, fc, d->rq); sL = wb_svf_lp(&d->fl, sL);
        wb_svf_set(&d->fr, fc, d->rq); sR = wb_svf_lp(&d->fr, sR);
    } else if (d->fmode == WB_F_BANDPASS) {
        wb_svf_set(&d->fl, d->cutoff, d->rq); sL = wb_svf_bp(&d->fl, sL);
        wb_svf_set(&d->fr, d->cutoff, d->rq); sR = wb_svf_bp(&d->fr, sR);
    }
    /* pitch-shifted taps (WARP C) */
    if (d->pitch < 0.99f || d->pitch > 1.01f) {
        sL = wb_pshift_process(&d->pl, sL, d->pitch);
        sR = wb_pshift_process(&d->pr, sR, d->pitch);
    }
    /* write input + feedback (tap1) */
    d->l[d->w] = wb_softclip(inL + t1L * d->fb);
    d->r[d->w] = wb_softclip(inR + t1R * d->fb);
    if (++d->w >= d->len) d->w = 0;

    *outL = sL * d->level;
    *outR = sR * d->level;
}

#endif /* WB_MULTITAP_H */
