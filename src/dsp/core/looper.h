/* looper.h — 60s phrase looper with a base layer + a separate overdub layer
 * (so Undo clears only the overdub). Record / play / overdub, reverse, varispeed,
 * loop fade-shape, quantize-to-beat. Buffers are engine-owned int16 (bounds RAM).
 *
 * State machine: 0 idle, 1 init-record, 2 play, 3 overdub. */
#ifndef WB_LOOPER_H
#define WB_LOOPER_H

#include <string.h>
#include <stdint.h>
#include "util.h"
#include "wav.h"

enum { WB_LP_IDLE = 0, WB_LP_REC = 1, WB_LP_PLAY = 2, WB_LP_DUB = 3 };
enum { WB_FADE_INOUT = 0, WB_FADE_IN = 1, WB_FADE_OUT = 2 };

typedef struct {
    int16_t *base_l, *base_r, *od_l, *od_r;   /* engine-owned, length = cap */
    int cap;
    int state;
    int writepos;        /* init-record linear head */
    double ph;           /* play/overdub head (samples, fractional) */
    int frames;          /* loop length once closed */
    int dir;             /* +1 fwd, -1 reverse */
    float speed;
    float level, level_cur, fade;
    int fademode;
    int only;            /* mute the dry thru (Looper Only) */
} wb_looper_t;

static inline void wb_looper_init(wb_looper_t *lp, int16_t *bl, int16_t *br,
                                  int16_t *ol, int16_t *or_, int cap) {
    lp->base_l = bl; lp->base_r = br; lp->od_l = ol; lp->od_r = or_;
    lp->cap = cap; lp->state = WB_LP_IDLE; lp->writepos = 0; lp->ph = 0.0;
    lp->frames = 0; lp->dir = 1; lp->speed = 1.0f;
    lp->level = 0.9f; lp->level_cur = 0.9f; lp->fade = 0.1f;
    lp->fademode = WB_FADE_INOUT; lp->only = 0;
}

/* attach lazily-allocated buffers (idle instances keep these NULL = 0 RAM) */
static inline void wb_looper_attach(wb_looper_t *lp, int16_t *bl, int16_t *br,
                                    int16_t *ol, int16_t *or_, int cap) {
    lp->base_l = bl; lp->base_r = br; lp->od_l = ol; lp->od_r = or_; lp->cap = cap;
}

static inline void wb_looper_clear(wb_looper_t *lp) {
    if (!lp->base_l) { lp->state = WB_LP_IDLE; lp->frames = 0; lp->writepos = 0; lp->ph = 0.0; return; }
    memset(lp->base_l, 0, sizeof(int16_t) * lp->cap);
    memset(lp->base_r, 0, sizeof(int16_t) * lp->cap);
    memset(lp->od_l, 0, sizeof(int16_t) * lp->cap);
    memset(lp->od_r, 0, sizeof(int16_t) * lp->cap);
    lp->state = WB_LP_IDLE; lp->frames = 0; lp->writepos = 0; lp->ph = 0.0;
}
/* save the loop (base + overdub mixed down) to a WAV; returns 0 ok */
static inline int wb_looper_save(wb_looper_t *lp, const char *path) {
    if (!lp->base_l || lp->frames < 1) return -1;
    /* mix overdub into base in place (overdubs are mixed down on save) */
    for (int i = 0; i < lp->frames; i++) {
        float ml = wb_i16_to_f(lp->base_l[i]) + wb_i16_to_f(lp->od_l[i]);
        float mr = wb_i16_to_f(lp->base_r[i]) + wb_i16_to_f(lp->od_r[i]);
        lp->base_l[i] = wb_f_to_i16(wb_softclip(ml));
        lp->base_r[i] = wb_f_to_i16(wb_softclip(mr));
    }
    memset(lp->od_l, 0, sizeof(int16_t) * lp->cap);
    memset(lp->od_r, 0, sizeof(int16_t) * lp->cap);
    return wb_wav_write(path, lp->base_l, lp->base_r, lp->frames);
}

/* load a loop WAV into the base layer; clears overdub; starts playback. */
static inline int wb_looper_load(wb_looper_t *lp, const char *path) {
    if (!lp->base_l) return -1;
    memset(lp->base_l, 0, sizeof(int16_t) * lp->cap);
    memset(lp->base_r, 0, sizeof(int16_t) * lp->cap);
    memset(lp->od_l, 0, sizeof(int16_t) * lp->cap);
    memset(lp->od_r, 0, sizeof(int16_t) * lp->cap);
    int fr = wb_wav_read(path, lp->base_l, lp->base_r, lp->cap);
    if (fr < 1) { lp->state = WB_LP_IDLE; lp->frames = 0; return -1; }
    lp->frames = fr; lp->ph = 0.0; lp->state = WB_LP_PLAY;
    return 0;
}

static inline void wb_looper_undo(wb_looper_t *lp) {
    if (!lp->od_l) return;
    memset(lp->od_l, 0, sizeof(int16_t) * lp->cap);
    memset(lp->od_r, 0, sizeof(int16_t) * lp->cap);
    if (lp->state == WB_LP_DUB) lp->state = WB_LP_PLAY;
}

static inline void wb_looper_rec_start(wb_looper_t *lp) {
    if (!lp->base_l) return;               /* buffers not allocated yet — caller must ensure first */
    wb_looper_clear(lp);
    lp->state = WB_LP_REC; lp->writepos = 0;
}

/* close the initial recording at the current length (already quantized by caller) */
static inline void wb_looper_close(wb_looper_t *lp, int frames) {
    if (frames < 64) frames = 64;
    if (frames > lp->cap) frames = lp->cap;
    lp->frames = frames; lp->ph = 0.0; lp->state = WB_LP_PLAY;
}

static inline float wb_looper_rd(const int16_t *b, int frames, double ph) {
    int i0 = (int)ph; int i1 = i0 + 1; if (i1 >= frames) i1 = 0;
    float f = (float)(ph - (double)i0);
    return wb_lerpf(wb_i16_to_f(b[i0]), wb_i16_to_f(b[i1]), f);
}

/* boundary fade envelope by position in the loop */
static inline float wb_looper_env(const wb_looper_t *lp) {
    float fadeFr = wb_clampf(lp->fade * WB_SR, 1.0f, (float)lp->frames * 0.5f);
    float pos = (float)lp->ph;
    int inEn = (lp->fademode != WB_FADE_OUT);
    int outEn = (lp->fademode != WB_FADE_IN);
    float up = inEn ? wb_clampf(pos / fadeFr, 0.0f, 1.0f) : 1.0f;
    float dn = outEn ? wb_clampf(((float)lp->frames - pos) / fadeFr, 0.0f, 1.0f) : 1.0f;
    return up * dn;
}

/* process one sample. thru = processed mix; rec = source to record (pre or post,
 * selected by the engine). Writes final out to outL/outR. Returns 1 if init-record
 * just hit the buffer cap (caller should auto-close). */
static inline int wb_looper_process(wb_looper_t *lp, float thruL, float thruR,
                                    float recL, float recR,
                                    float *outL, float *outR) {
    int hit_cap = 0;
    float playL = 0.0f, playR = 0.0f;

    if (!lp->base_l) { *outL = thruL; *outR = thruR; return 0; }   /* lazy: no buffer => pass-thru */

    if (lp->state == WB_LP_REC) {
        lp->base_l[lp->writepos] = wb_f_to_i16(recL);
        lp->base_r[lp->writepos] = wb_f_to_i16(recR);
        if (++lp->writepos >= lp->cap) { lp->writepos = lp->cap; hit_cap = 1; }
    } else if (lp->state == WB_LP_PLAY || lp->state == WB_LP_DUB) {
        int idx = (int)lp->ph; if (idx < 0) idx = 0; if (idx >= lp->frames) idx = lp->frames - 1;
        if (lp->state == WB_LP_DUB) {
            float nl = wb_i16_to_f(lp->od_l[idx]) + recL;
            float nr = wb_i16_to_f(lp->od_r[idx]) + recR;
            lp->od_l[idx] = wb_f_to_i16(wb_softclip(nl));
            lp->od_r[idx] = wb_f_to_i16(wb_softclip(nr));
        }
        float bl = wb_looper_rd(lp->base_l, lp->frames, lp->ph);
        float br = wb_looper_rd(lp->base_r, lp->frames, lp->ph);
        float ol = wb_looper_rd(lp->od_l, lp->frames, lp->ph);
        float orr = wb_looper_rd(lp->od_r, lp->frames, lp->ph);
        float env = wb_looper_env(lp);
        playL = (bl + ol) * env;
        playR = (br + orr) * env;
        lp->ph += (double)(lp->dir * lp->speed);
        while (lp->ph >= (double)lp->frames) lp->ph -= (double)lp->frames;
        while (lp->ph < 0.0) lp->ph += (double)lp->frames;
    }

    lp->level_cur += (lp->level - lp->level_cur) * 0.002f;
    float thruGain = lp->only ? 0.0f : 1.0f;
    *outL = thruL * thruGain + playL * lp->level_cur;
    *outR = thruR * thruGain + playR * lp->level_cur;
    return hit_cap;
}

#endif /* WB_LOOPER_H */
