/* pitchshift.h — compact time-domain pitch shifter (two read heads, overlap-add).
 * Used by the multitap delay for WARP's pitch-shifted taps. Self-contained buffer. */
#ifndef WB_PITCHSHIFT_H
#define WB_PITCHSHIFT_H

#include <math.h>
#include "util.h"

#define WB_PS_LEN 4096   /* ~93 ms window at 44.1k */

typedef struct {
    float buf[WB_PS_LEN];
    int   w;
    float r;
} wb_pshift_t;

static inline void wb_pshift_init(wb_pshift_t *p) {
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
    float a = sinf((float)M_PI * (gap1 / WB_PS_LEN));
    float b = sinf((float)M_PI * (gap2 / WB_PS_LEN));

    float out = (wb_ps_read(p, p->r) * a + wb_ps_read(p, r2) * b) / (a + b + 1e-6f);
    if (++p->w >= WB_PS_LEN) p->w = 0;
    return out;
}

#endif /* WB_PITCHSHIFT_H */
