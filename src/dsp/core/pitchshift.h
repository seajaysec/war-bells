/* pitchshift.h — compact time-domain pitch shifter (two read heads, overlap-add).
 * Used by the multitap delay (WARP) and the shimmer reverb. Self-contained buffer.
 *
 * The overlap-add window is a precomputed LUT (no per-sample sinf) — this keeps the
 * per-sample cost to a couple of table lookups + lerps, which matters under load when
 * several of these run alongside dense granular voices (live-gig CPU headroom). */
#ifndef WB_PITCHSHIFT_H
#define WB_PITCHSHIFT_H

#include <math.h>
#include "util.h"

#define WB_PS_LEN 4096   /* ~93 ms window at 44.1k */

/* shared, read-only after init: WB_PS_WIN[g] = sin(pi * g / WB_PS_LEN) */
static float WB_PS_WIN[WB_PS_LEN];
static int   WB_PS_WIN_READY = 0;
static inline void wb_ps_win_init(void) {
    if (WB_PS_WIN_READY) return;
    for (int i = 0; i < WB_PS_LEN; i++) WB_PS_WIN[i] = sinf((float)M_PI * (float)i / (float)WB_PS_LEN);
    WB_PS_WIN_READY = 1;
}

typedef struct {
    float buf[WB_PS_LEN];
    int   w;
    float r;
} wb_pshift_t;

static inline void wb_pshift_init(wb_pshift_t *p) {
    wb_ps_win_init();
    for (int i = 0; i < WB_PS_LEN; i++) p->buf[i] = 0.0f;
    p->w = 0; p->r = 0.0f;
}

static inline float wb_ps_read(const wb_pshift_t *p, float pos) {
    while (pos < 0.0f) pos += WB_PS_LEN;
    while (pos >= WB_PS_LEN) pos -= WB_PS_LEN;
    int i0 = (int)pos; int i1 = i0 + 1; if (i1 >= WB_PS_LEN) i1 = 0;
    float f = pos - (float)i0;
    return wb_lerpf(p->buf[i0], p->buf[i1], f);
}

static inline float wb_pshift_process(wb_pshift_t *p, float x, float ratio) {
    p->buf[p->w] = x;
    p->r += ratio;
    while (p->r >= WB_PS_LEN) p->r -= WB_PS_LEN;
    while (p->r < 0.0f) p->r += WB_PS_LEN;

    float r2 = p->r + (WB_PS_LEN * 0.5f);
    if (r2 >= WB_PS_LEN) r2 -= WB_PS_LEN;

    float gap1 = (float)p->w - p->r;  if (gap1 < 0) gap1 += WB_PS_LEN;
    float gap2 = (float)p->w - r2;    if (gap2 < 0) gap2 += WB_PS_LEN;
    int   g1 = (int)gap1; if (g1 < 0) g1 = 0; else if (g1 >= WB_PS_LEN) g1 = WB_PS_LEN - 1;
    int   g2 = (int)gap2; if (g2 < 0) g2 = 0; else if (g2 >= WB_PS_LEN) g2 = WB_PS_LEN - 1;
    float a = WB_PS_WIN[g1];          /* LUT — no per-sample sinf */
    float b = WB_PS_WIN[g2];

    float out = (wb_ps_read(p, p->r) * a + wb_ps_read(p, r2) * b) / (a + b + 1e-6f);
    if (++p->w >= WB_PS_LEN) p->w = 0;
    if (out > -1e-20f && out < 1e-20f) out = 0.0f;   /* denormal flush */
    return out;
}

#endif /* WB_PITCHSHIFT_H */
