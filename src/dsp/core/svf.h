/* svf.h — Andrew Simper TPT state-variable filter (stable, zero-delay feedback).
 * Used as the global resonant low-pass (DFM1 analogue) and per-voice LP/BP/sweep. */
#ifndef WB_SVF_H
#define WB_SVF_H

#include <math.h>
#include "util.h"

typedef struct {
    float ic1eq, ic2eq;   /* integrator state */
    float g, k, a1, a2, a3;
} wb_svf_t;

static inline void wb_svf_reset(wb_svf_t *f) {
    f->ic1eq = 0.0f; f->ic2eq = 0.0f;
}

/* cutoff in Hz, res 0..1 (-> Q). Recompute coeffs when params change. */
static inline void wb_svf_set(wb_svf_t *f, float cutoff, float res) {
    cutoff = wb_clampf(cutoff, 20.0f, 20000.0f);
    res = wb_clampf(res, 0.0f, 0.98f);
    float q = 0.5f + res * 9.5f;           /* Q 0.5 .. 10 */
    f->g = tanf((float)M_PI * cutoff / WB_SR);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + f->g * (f->g + f->k));
    f->a2 = f->g * f->a1;
    f->a3 = f->g * f->a2;
}

/* returns low-pass output */
static inline float wb_svf_lp(wb_svf_t *f, float v0) {
    float v3 = v0 - f->ic2eq;
    float v1 = f->a1 * f->ic1eq + f->a2 * v3;
    float v2 = f->ic2eq + f->a2 * f->ic1eq + f->a3 * v3;
    f->ic1eq = 2.0f * v1 - f->ic1eq;
    f->ic2eq = 2.0f * v2 - f->ic2eq;
    return v2;
}

/* returns band-pass output (uses same state; call ONE of lp/bp per sample) */
static inline float wb_svf_bp(wb_svf_t *f, float v0) {
    float v3 = v0 - f->ic2eq;
    float v1 = f->a1 * f->ic1eq + f->a2 * v3;
    float v2 = f->ic2eq + f->a2 * f->ic1eq + f->a3 * v3;
    f->ic1eq = 2.0f * v1 - f->ic1eq;
    f->ic2eq = 2.0f * v2 - f->ic2eq;
    return v1;
}

#endif /* WB_SVF_H */
