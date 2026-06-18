/* transient.h — onset detector for the note-onset-driven effects
 * (onset-driven effects — Arp, Chain). Compares a fast vs slow amplitude envelope and emits
 * a one-sample pulse on attacks, with a refractory hold to avoid double-fires. */
#ifndef WB_TRANSIENT_H
#define WB_TRANSIENT_H

#include <math.h>
#include "util.h"

typedef struct {
    float env_fast, env_slow;
    int   refractory;
} wb_transient_t;

static inline void wb_transient_init(wb_transient_t *t) {
    t->env_fast = 0.0f; t->env_slow = 0.0f; t->refractory = 0;
}

/* returns 1 on a detected onset, else 0 */
static inline int wb_transient_process(wb_transient_t *t, float mono) {
    float a = fabsf(mono);
    t->env_fast += (a - t->env_fast) * 0.20f;    /* ~ fast attack */
    t->env_slow += (a - t->env_slow) * 0.0015f;  /* slow baseline */
    int onset = 0;
    if (t->refractory > 0) {
        t->refractory--;
    } else if (t->env_fast > t->env_slow * 1.6f + 0.01f) {
        onset = 1;
        t->refractory = (int)(0.04f * WB_SR);     /* 40 ms lockout */
    }
    return onset;
}

#endif /* WB_TRANSIENT_H */
