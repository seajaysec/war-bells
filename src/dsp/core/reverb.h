/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * War Bells — granular/glitch/delay/looper audio FX for Ableton Move
 * Copyright (C) 2026 Chris Farrell
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version. It is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU AGPL <https://www.gnu.org/licenses/> for more details.
 */

/* reverb.h — Schroeder-Moorer reverb (adapted from the framework's Freeverb,
 * itself public-domain Freeverb by Jezar). Drives the 4 reverb modes
 * via room_size/damping presets. Returns WET only; the engine blends with `space`. */
#ifndef WB_REVERB_H
#define WB_REVERB_H

#include <string.h>
#include "util.h"

#define WB_RV_COMBS 8
#define WB_RV_APS   4
#define WB_RV_MAXD  2048

static const int wb_comb_l[WB_RV_COMBS] = {1116,1188,1277,1356,1422,1491,1557,1617};
static const int wb_comb_r[WB_RV_COMBS] = {1139,1211,1300,1379,1445,1514,1580,1640};
static const int wb_ap_l[WB_RV_APS]     = {556,441,341,225};
static const int wb_ap_r[WB_RV_APS]     = {579,464,364,248};

typedef struct { float buf[WB_RV_MAXD]; int size, idx; float store; } wb_comb_t;
typedef struct { float buf[WB_RV_MAXD]; int size, idx; } wb_ap_t;

typedef struct {
    wb_comb_t cl[WB_RV_COMBS], cr[WB_RV_COMBS];
    wb_ap_t   al[WB_RV_APS],   ar[WB_RV_APS];
    float room_size, damping, width;
    float feedback, damp1, damp2;
    float dcx, dcy;           /* input DC blocker state (combs at ~0.94 fb amplify DC ~16x) */
    int   eco;                /* Eco: run 4 combs instead of 8 (CPU mode, thinner tail) */
} wb_reverb_t;

static inline void wb_reverb_init(wb_reverb_t *v) {
    memset(v, 0, sizeof(*v));
    for (int i = 0; i < WB_RV_COMBS; i++) {
        v->cl[i].size = wb_comb_l[i]; v->cr[i].size = wb_comb_r[i];
    }
    for (int i = 0; i < WB_RV_APS; i++) {
        v->al[i].size = wb_ap_l[i]; v->ar[i].size = wb_ap_r[i];
    }
    v->room_size = 0.6f; v->damping = 0.4f; v->width = 1.0f;
}

static inline void wb_reverb_update(wb_reverb_t *v) {
    v->feedback = v->room_size * 0.28f + 0.7f;
    v->damp1 = v->damping * 0.4f;
    v->damp2 = 1.0f - v->damp1;
}

/* size 0..1, damp 0..1 (set per reverb mode) */
static inline void wb_reverb_set(wb_reverb_t *v, float size, float damp) {
    v->room_size = wb_clampf(size, 0.0f, 1.0f);
    v->damping = wb_clampf(damp, 0.0f, 1.0f);
    wb_reverb_update(v);
}

static inline float wb_comb_run(wb_comb_t *c, float in, float fb, float d1, float d2) {
    float out = c->buf[c->idx];
    c->store = (out * d2) + (c->store * d1);
    c->buf[c->idx] = in + (c->store * fb);
    if (++c->idx >= c->size) c->idx = 0;
    return out;
}
static inline float wb_ap_run(wb_ap_t *a, float in) {
    float bo = a->buf[a->idx];
    float out = -in + bo;
    a->buf[a->idx] = in + (bo * 0.5f);
    if (++a->idx >= a->size) a->idx = 0;
    return out;
}

/* returns WET stereo in outL/outR */
static inline void wb_reverb_process(wb_reverb_t *v, float inL, float inR,
                                     float *outL, float *outR) {
    float input = (inL + inR) * 0.5f;
    /* DC blocker on the input — the high-feedback combs (~0.94 in Hall/Vast) amplify any DC ~16x, which
     * otherwise rides up over seconds into an offset (the harness flagged 0.155). One-pole HPF ~5 Hz. */
    float dci = input - v->dcx + 0.9995f * v->dcy; v->dcx = input; v->dcy = dci; input = dci;
    float oL = 0.0f, oR = 0.0f;
    /* Eco halves the comb bank (the memory-bound bulk of the reverb cost) — thinner tail, ~half
     * the reverb CPU. Default (eco=0) runs the full 8 combs = unchanged sound. */
    int nc = v->eco ? (WB_RV_COMBS / 2) : WB_RV_COMBS;
    for (int i = 0; i < nc; i++) {
        oL += wb_comb_run(&v->cl[i], input, v->feedback, v->damp1, v->damp2);
        oR += wb_comb_run(&v->cr[i], input, v->feedback, v->damp1, v->damp2);
    }
    float norm = v->eco ? 0.25f : 0.125f;   /* 1/4 combs vs 1/8 — level-match across the toggle */
    oL *= norm; oR *= norm;
    for (int i = 0; i < WB_RV_APS; i++) {
        oL = wb_ap_run(&v->al[i], oL);
        oR = wb_ap_run(&v->ar[i], oR);
    }
    float w1 = v->width / 2.0f + 0.5f;
    float w2 = (1.0f - v->width) / 2.0f;
    *outL = oL * w1 + oR * w2;
    *outR = oR * w1 + oL * w2;
}

#endif /* WB_REVERB_H */
