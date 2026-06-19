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

/* wb_internal.h — War Bells instance state + shared enums/constants.
 *
 * War Bells is a Schwung (Ableton Move) granular / glitch / delay / looper multi-effect:
 * 11 effects / 44 A-D variations of granular + multitap delay, with global
 * pitch-mod, 4-mode reverb, resonant LP filter, Hold Sampler and a 60s phrase
 * looper. DSP ported from a sibling monome norns build. */
#ifndef WB_INTERNAL_H
#define WB_INTERNAL_H

#include <stdint.h>
#include "host/plugin_api_v1.h"
#include "core/util.h"
#include "core/ringbuf.h"
#include "core/grain.h"
#include "core/multitap.h"
#include "core/chorus.h"
#include "core/reverb.h"
#include "core/svf.h"
#include "core/looper.h"
#include "core/transient.h"

#define WB_NV          6          /* granular voices */
#define WB_CAP_SEC     5.0f       /* live-input capture for grains */
#define WB_DELAY_SEC   4.0f       /* multitap delay line */
#define WB_LOOP_SEC    60.0f      /* phrase looper */
#define WB_SPDL_LEN    44100      /* space delay (1 s) */

#define WB_NEFFECTS    11

/* looper transport commands (momentary enum) */
enum { WB_T_IDLE=0, WB_T_REC=1, WB_T_PLAY=2, WB_T_DUB=3, WB_T_STOP=4, WB_T_ERASE=5,
       WB_T_UNDO=6, WB_T_SAVE=7, WB_T_LOAD=8 };

typedef struct wb {
    const host_api_v1_t *host;

    /* ---- parameters (raw user values) ---- */
    int   effect, variation;          /* 0..10, 0..3 */
    float activity, repeats, shape;
    float filter, filter_res;
    float mix, effect_vol;
    float space; int reverb_mode;
    float sustain;                    /* space-delay feedback toward unity (sound-on-sound "storing up") */
    float warp;                       /* 0.5 = neutral; swept delay-time = tape pitch bend (rho = 1 - D') */
    float mod_depth, mod_rate;
    int   subdiv;                     /* 0..5 */
    float tempo_manual; int tempo_src; /* 0 host, 1 manual */
    int   hold, hold_style, reverse, bypass, bypass_style;
    int   bypass_trails;              /* Trails: bypass lets the delay/reverb tail ring out (Avalanche-style) */
    int   eco;                        /* Eco CPU mode: lighter reverb (4 combs) for stacking instances */
    int   input_mono; float input_gain;
    char  module_dir[512];

    /* looper params */
    int   looper_on, loop_reverse, loop_route, loop_order, loop_quantize, loop_only, loop_burst;
    float loop_level, loop_fade, loop_speed; int loop_fademode;

    /* user-preset bank (params + loop audio per slot, saved under module_dir/presets) */
    int   user_slot;                  /* 1..16 target slot */

    /* ---- DSP state ---- */
    wb_ring_t   ring;
    float       *cap_l, *cap_r;
    wb_voice_t  voices[WB_NV];
    wb_delay_t  delay;
    float       *dl_l, *dl_r;
    wb_chorus_t chorus;
    wb_reverb_t reverb;
    wb_svf_t    filt_l, filt_r;       /* global resonant LP */
    float       fcut, fres;
    wb_looper_t looper;
    int16_t     *lp_bl, *lp_br, *lp_ol, *lp_or;
    wb_transient_t trans;

    /* space delay (feedback comb) */
    wb_tlim_t   tlim_l, tlim_r;       /* master tape-ceiling limiter state (per channel) */
    float spdl_l[WB_SPDL_LEN], spdl_r[WB_SPDL_LEN];
    int   spdl_w; float space_dtime, space_fb;
    /* tape/feedback engine state (Sustain build + Warp pitch): smoothed read offset, loop tone LPF,
     * loop DC blocker. warp_eff = per-block warp amount (knob + Motion). */
    double spdl_smooth; float warp_eff;
    float sp_lp_l, sp_lp_r, sp_dcx_l, sp_dcy_l, sp_dcx_r, sp_dcy_r;

    /* shimmer reverb: ONE mono pitch-shifter fed back into the reverb tail (bounded) */
    int   shimmer;                    /* 0 Off 1 Oct+ 2 Oct- 3 5th */
    wb_pshift_t shimM;
    float shim_m;

    /* runtime */
    int   grain_env;                  /* 0 Soft 1 Pluck 2 Swell 3 Gate */
    int   pitch_scale;                /* 0 off, else snap grain pitch to a scale */

    /* Motion: one tempo-synced LFO modulating a chosen macro */
    int   mot_target;                 /* 0 Off 1 Act 2 Filt 3 Space 4 Mix 5 Mod */
    int   mot_rate;                   /* division index (8bar..1/8) */
    int   mot_shape;                  /* 0 Sine 1 Tri 2 Ramp 3 Rand */
    float mot_depth;                  /* 0..1 */
    double mot_phase; uint32_t mot_rng; float mot_rng_val; int mot_newrand;

    /* Evolve / Dice: clocked, bounded re-rolling of texture for generative drift */
    float evolve;                     /* 0 off .. 1 fast drift */
    int   evo_range;                  /* 0 Soft 1 Mid 2 Wild */
    double evo_acc; uint32_t evo_rng;

    float duck;                       /* 0..1 sidechain wet to input level (blooms in gaps) */
    float width;                      /* reverb stereo width (1.0 = current wide default) */
    int   preset;                     /* last-selected character preset (display) */
    float tempo_drift;                /* Free-mode bounded random walk on tempo */
    uint32_t drift_rng;
    int   params_dirty;
    float cur_tempo;
    float bypass_cur;                 /* crossfade lag */
    float bypass_lag;
    int   midi_onset;                 /* set by on_midi note-on, consumed per block */
} wb_t;

/* effects.c — metadata + the brain */
typedef struct {
    char  id;            /* 'A'..'D' (internal) */
    const char *label;   /* short <=6-char functional label shown on screen */
    const char *note;    /* longer description (help / docs) */
    int   kind;          /* 0 grain, 1 delay */
    float speeds[4]; int nspeeds;
    int   glide; float glo, ghi; int bidir;
    int   filt;          /* WB_F_* */
    float crush;         /* 0..1 */
    float scatter, spread, grainsize;
    int   drone, sub, onset;
    int   pattern, pitchtap, graintap;
} wb_var_t;

typedef struct {
    const char *name; int cat; const char *activity;
    wb_var_t vars[4];
} wb_effect_t;

extern const wb_effect_t WB_EFFECTS[WB_NEFFECTS];
extern const char *WB_CATEGORIES[4];

float wb_loopsec(const wb_t *w);
void  wb_apply_effect(wb_t *w);
void  wb_apply_tone(wb_t *w);
void  wb_apply_space(wb_t *w);
void  wb_apply_all(wb_t *w);
void  wb_evolve_roll(wb_t *w, int range);   /* generative re-roll (Evolve clock + Dice) */

/* params.c */
void wb_params_defaults(wb_t *w);
void wb_params_set(wb_t *w, const char *key, const char *val);
int  wb_params_get(wb_t *w, const char *key, char *buf, int buf_len);

#endif /* WB_INTERNAL_H */
