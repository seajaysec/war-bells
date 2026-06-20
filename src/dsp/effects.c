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

/* effects.c — the 11 effects / 44 A-D variations (ported from effects.lua) and the
 * "musical brain" (ported from mc_engine.lua) that turns the high-level macros into
 * concrete per-voice / delay / reverb / chorus configuration. */
#include <math.h>
#include "wb_internal.h"

const char *WB_CATEGORIES[4] = { "Loops", "Grains", "Glitch", "Delays" };

/* base+variation already merged into each entry (cat: 1 microloop,2 granules,3 glitch,4 multidelay) */
const wb_effect_t WB_EFFECTS[WB_NEFFECTS] = {
 /* ---- GLITCH ---- */
 { "Arp", 3, "arpeggio steps", {
   {'A',"basic","basic arpeggios of recent samples",0,{1},1,0,1,1,0,WB_F_NONE,0,0.15f,0.10f,0,0,0,1,0,0,0},
   {'B',"spds","varied playback speeds",0,{0.5f,1,2},3,0,1,1,0,WB_F_NONE,0,0.15f,0.10f,0,0,0,1,0,0,0},
   {'C',"filt","random filter per step",0,{1},1,0,1,1,0,WB_F_RANDOM,0,0.15f,0.10f,0,0,0,1,0,0,0},
   {'D',"crush","bit-crushed arpeggios",0,{1},1,0,1,1,0,WB_F_NONE,0.5f,0.15f,0.10f,0,0,0,1,0,0,0}}},
 { "Cutup", 3, "variation & manipulation", {
   {'A',"cuts","glitches interrupt live signal",0,{1},1,0,1,1,0,WB_F_NONE,0,0.80f,0.20f,0,0,0,0,0,0,0},
   {'B',"pitch","pitch-shifted interruptions",0,{0.5f,1,2},3,0,1,1,0,WB_F_NONE,0,0.80f,0.20f,0,0,0,0,0,0,0},
   {'C',"sweep","filter sweeps + delay",0,{1},1,0,1,1,0,WB_F_SWEEP,0,0.80f,0.20f,0,0,0,0,0,0,0},
   {'D',"crush","bit-crush + drastic manip",0,{0.5f,1},2,0,1,1,0,WB_F_NONE,0.7f,0.80f,0.20f,0,0,0,0,0,0,0}}},
 { "Chop", 3, "variation & manipulation", {
   {'A',"runs","rearranges + sequenced runs",0,{1},1,0,1,1,0,WB_F_NONE,0,0.70f,0.15f,0,0,0,1,0,0,0},
   {'B',"pitch","rearranges + pitch-shifts",0,{0.5f,1,2},3,0,1,1,0,WB_F_NONE,0,0.70f,0.15f,0,0,0,1,0,0,0},
   {'C',"soft","softer filtered glitch",0,{1},1,0,1,1,0,WB_F_SWEEP,0,0.70f,0.15f,0,0,0,1,0,0,0},
   {'D',"crush","pitch-shift + bit-crush",0,{0.5f,1,2},3,0,1,1,0,WB_F_NONE,0.6f,0.70f,0.15f,0,0,0,1,0,0,0}}},
 /* ---- MICRO LOOP ---- */
 { "Glide", 1, "rate of pitch-shifting", {
   {'A',"half","glides half<->normal",0,{1},1,1,0.5f,1.0f,0,WB_F_NONE,0,0.10f,0.25f,0,0,0,0,0,0,0},
   {'B',"down","glides double<->half",0,{1},1,1,2.0f,0.5f,0,WB_F_NONE,0,0.10f,0.25f,0,0,0,0,0,0,0},
   {'C',"up","glides normal<->double",0,{1},1,1,1.0f,2.0f,0,WB_F_NONE,0,0.10f,0.25f,0,0,0,0,0,0,0},
   {'D',"both","glides both directions",0,{1},1,1,0.5f,2.0f,1,WB_F_NONE,0,0.10f,0.25f,0,0,0,0,0,0,0}}},
 { "Seq", 1, "filter variation / layers", {
   {'A',"filt","filtered random rhythms",0,{1},1,0,1,1,0,WB_F_RANDOM,0,0.40f,0.15f,0,0,0,0,0,0,0},
   {'B',"half","alternates normal/half",0,{1,0.5f},2,0,1,1,0,WB_F_NONE,0,0.40f,0.15f,0,0,0,0,0,0,0},
   {'C',"sweep","overlapping + filter sweeps",0,{1},1,0,1,1,0,WB_F_SWEEP,0,0.40f,0.15f,0,0,0,0,0,0,0},
   {'D',"crush","interlocking + bit-crush",0,{1},1,0,1,1,0,WB_F_NONE,0.4f,0.40f,0.15f,0,1,0,0,0,0,0}}},
 { "Stack", 1, "number of active loopers", {
   {'A',"oct+","normal + double (oct up)",0,{1,2},2,0,1,1,0,WB_F_NONE,0,0.05f,0.10f,0,0,0,0,0,0,0},
   {'B',"oct-","normal + half (oct down)",0,{1,0.5f},2,0,1,1,0,WB_F_NONE,0,0.05f,0.10f,0,0,0,0,0,0,0},
   {'C',"x2","all loops double speed",0,{2},1,0,1,1,0,WB_F_NONE,0,0.05f,0.10f,0,0,0,0,0,0,0},
   {'D',"range","half/normal/double/quad",0,{0.5f,1,2,4},4,0,1,1,0,WB_F_NONE,0,0.05f,0.10f,0,0,0,0,0,0,0}}},
 /* ---- GRANULES ---- */
 { "Cloud", 2, "grain density & spread", {
   {'A',"short","short diffused stretches",0,{1},1,0,1,1,0,WB_F_NONE,0,0.50f,0.90f,0.12f,0,0,0,0,0,0},
   {'B',"dense","many randomized grains",0,{1},1,0,1,1,0,WB_F_NONE,0,0.50f,1.00f,0.18f,0,0,0,0,0,0},
   {'C',"hi","normal + double grains",0,{1,2},2,0,1,1,0,WB_F_NONE,0,0.50f,0.90f,0.18f,0,0,0,0,0,0},
   {'D',"lo","normal + half grains",0,{1,0.5f},2,0,1,1,0,WB_F_NONE,0,0.50f,0.90f,0.18f,0,0,0,0,0,0}}},
 { "Drone", 2, "depth of each modifier", {
   {'A',"morph","drone length morphs",0,{1},1,0,1,1,0,WB_F_NONE,0,0.02f,0.20f,0,1,0,0,0,0,0},
   {'B',"sub","sub-octave + sweeps",0,{0.5f},1,0,1,1,0,WB_F_SWEEP,0,0.02f,0.20f,0,1,1,0,0,0,0},
   {'C',"bpf","resonant bandpass drone",0,{1},1,0,1,1,0,WB_F_BANDPASS,0,0.02f,0.20f,0,1,0,0,0,0,0},
   {'D',"env","envelope length mod",0,{1},1,0,1,1,0,WB_F_NONE,0,0.02f,0.20f,0,1,0,0,0,0,0}}},
 { "Chain", 2, "density of pattern", {
   {'A',"hold","repeats recent note",0,{1},1,0,1,1,0,WB_F_NONE,0,0.10f,0.30f,0,0,0,1,0,0,0},
   {'B',"phase","overlapping phasing",0,{1},1,0,1,1,0,WB_F_NONE,0,0.10f,0.30f,0,0,0,1,0,0,0},
   {'C',"casc","cascading chains",0,{1},1,0,1,1,0,WB_F_NONE,0,0.10f,0.30f,0,0,0,1,0,0,0},
   {'D',"casc2","cascades + double grains",0,{1,2},2,0,1,1,0,WB_F_NONE,0,0.10f,0.30f,0,0,0,1,0,0,0}}},
 /* ---- MULTIDELAY ---- */
 { "Taps", 4, "number of active delay taps", {
   {'A',"line","pattern 1 - linear delay",1,{1},1,0,1,1,0,WB_F_NONE,0,0,0,0,0,0,0,1,0,0},
   {'B',"swng","pattern 2",1,{1},1,0,1,1,0,WB_F_NONE,0,0,0,0,0,0,0,2,0,0},
   {'C',"trip","pattern 3",1,{1},1,0,1,1,0,WB_F_NONE,0,0,0,0,0,0,0,3,0,0},
   {'D',"wide","pattern 4",1,{1},1,0,1,1,0,WB_F_NONE,0,0,0,0,0,0,0,4,0,0}}},
 { "Warp", 4, "number of active delay taps", {
   {'A',"env","env filter per tap",1,{1},1,0,1,1,0,WB_F_SWEEP,0,0,0,0,0,0,0,1,0,0},
   {'B',"bpf","resonant bandpass taps",1,{1},1,0,1,1,0,WB_F_BANDPASS,0,0,0,0,0,0,0,2,0,0},
   {'C',"pitch","pitch-shifted taps",1,{1,2},2,0,1,1,0,WB_F_NONE,0,0,0,0,0,0,0,1,1,0},
   {'D',"grain","taps + double-speed grains",1,{1,2},2,0,1,1,0,WB_F_NONE,0,0,0,0,0,0,0,1,0,1}}},
};

static const float WB_SUBDIV_MULT[6] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };

/* reverb mode presets {room_size, damping} for {Bright Room, Dark Med, Large Hall, Ambient} */
static const float WB_REVERB_SIZE[4] = { 0.35f, 0.55f, 0.85f, 0.95f };
static const float WB_REVERB_DAMP[4] = { 0.15f, 0.55f, 0.35f, 0.25f };

static inline float clampf(float x, float lo, float hi){ return x<lo?lo:(x>hi?hi:x); }
static inline float lerpf(float a, float b, float t){ return a + (b-a)*t; }
static inline float crush_bits(float amt){ return clampf(24.0f - amt*20.0f, 3.0f, 24.0f); }

float wb_loopsec(const wb_t *w) {
    float beat = 60.0f / clampf(w->cur_tempo, 20.0f, 480.0f);
    return clampf(beat / WB_SUBDIV_MULT[w->subdiv], 0.03f, 4.0f);
}

void wb_apply_tone(wb_t *w) {
    w->chorus.depth = w->mod_depth;
    w->chorus.rate  = lerpf(0.2f, 9.0f, w->mod_rate);
    w->fcut = lerpf(120.0f, 19000.0f, powf(w->filter, 1.6f));   /* CW = open */
    w->fres = w->filter_res * 0.9f;
    wb_svf_set(&w->filt_l, w->fcut, w->fres);
    wb_svf_set(&w->filt_r, w->fcut, w->fres);
}

void wb_apply_space(wb_t *w) {
    int m = w->reverb_mode; if (m < 0) m = 0; if (m > 3) m = 3;
    wb_reverb_set(&w->reverb, WB_REVERB_SIZE[m], WB_REVERB_DAMP[m]);
    w->reverb.width = w->width;
    w->reverb.eco = w->eco;
    wb_reverb_set_tone(&w->reverb, w->rev_tone * 2.0f - 1.0f);   /* 0..1 knob -> bipolar -1..+1 */
    w->space_dtime = clampf(wb_loopsec(w), 0.05f, 1.0f);
    w->space_fb    = lerpf(0.15f, 0.6f, w->space);
}

static void apply_grain(wb_t *w, const wb_var_t *v) {
    float ls = wb_loopsec(w);
    float window = clampf(ls * WB_SR, 256.0f, WB_CAP_SEC * WB_SR - 64.0f);
    int nactive = (int)floorf(lerpf(1.0f, (float)WB_NV, w->activity) + 0.5f);
    if (v->drone) nactive = (int)ceilf(w->activity * WB_NV);
    if (nactive < 1) nactive = 1;
    if (nactive > WB_NV) nactive = WB_NV;
    if (w->eco && nactive > 4) nactive = 4;   /* Eco also caps granular voices (the #2 CPU cost) — only thins dense patches */

    float overlap = lerpf(1.2f, 4.5f, w->activity);
    float base_density = clampf((1.0f / ls) * overlap, 0.4f, 80.0f);
    if (v->spread > 0.8f) base_density *= 1.6f;

    float size = ls * lerpf(0.5f, 1.1f, w->shape);
    if (v->grainsize > 0.0f) size = v->grainsize * lerpf(0.6f, 1.6f, w->shape);
    if (v->drone) size = clampf(ls * lerpf(1.0f, 2.2f, w->repeats), 0.08f, 0.6f);
    size = clampf(size, 0.01f, 0.6f);

    for (int i = 0; i < WB_NV; i++) {
        wb_voice_t *vc = &w->voices[i];
        if (i < nactive) {
            float sp = v->speeds[i % v->nspeeds];
            if (w->reverse) sp = -sp;
            vc->gain    = (0.9f / sqrtf((float)nactive)) * lerpf(0.6f, 1.0f, w->repeats);
            vc->rate    = sp;
            vc->size    = size;
            vc->density = base_density * lerpf(0.7f, 1.3f, (float)(i + 1) / WB_NV);
            vc->scatter = v->scatter;
            vc->spread  = v->spread * lerpf(0.5f, 1.0f, w->activity);
            vc->crush   = crush_bits(v->crush);
            vc->fmode   = v->filt;
            vc->envmode = w->grain_env;
            vc->scale   = w->pitch_scale;
            vc->cutoff  = lerpf(1500.0f, 16000.0f, w->shape);
            vc->rq      = 0.5f;
            vc->window  = window;
            if (v->glide) {
                vc->glide = 1;
                vc->glo = v->glo * (w->reverse ? -1.0f : 1.0f);
                vc->ghi = v->ghi * (w->reverse ? -1.0f : 1.0f);
                vc->gfreq = lerpf(0.05f, 1.2f, w->activity);
                vc->bidir = (v->bidir && (i % 2)) ? 1 : 0;
            } else {
                vc->glide = 0;
            }
        } else {
            vc->gain = 0.0f; vc->glide = 0;
        }
    }
    w->delay.level = 0.0f;
}

static void apply_delay(wb_t *w, const wb_var_t *v) {
    for (int i = 0; i < WB_NV; i++) w->voices[i].gain = 0.0f;
    float base = clampf(wb_loopsec(w), 0.04f, 1.5f);
    int ntaps = (int)floorf(lerpf(1.0f, 4.0f, w->activity) + 0.5f);
    if (ntaps < 1) ntaps = 1;
    if (ntaps > 4) ntaps = 4;

    static const float PAT[4][4] = {
        {1.0f, 2.0f, 3.0f, 4.0f},
        {1.0f, 1.5f, 2.5f, 4.0f},
        {0.75f, 1.5f, 2.25f, 3.0f},
        {1.0f, 2.0f, 3.5f, 5.0f},
    };
    int p = (v->pattern >= 1 && v->pattern <= 4) ? v->pattern - 1 : 0;
    for (int i = 0; i < 4; i++) {
        w->delay.t[i] = clampf(base * PAT[p][i], 0.001f, 3.9f);
        w->delay.g[i] = (i < ntaps) ? lerpf(1.0f, 0.4f, (float)i / 3.0f) : 0.0f;
    }
    w->delay.fb     = lerpf(0.1f, 0.7f, w->repeats);
    w->delay.fmode  = v->filt;
    w->delay.cutoff = lerpf(800.0f, 16000.0f, w->shape);
    w->delay.rq     = 0.5f;
    w->delay.pitch  = (v->pitchtap || v->graintap) ? 2.0f : 1.0f;
    w->delay.level  = 1.0f;
}

void wb_apply_effect(wb_t *w) {
    int e = w->effect; if (e < 0) e = 0; if (e >= WB_NEFFECTS) e = WB_NEFFECTS - 1;
    int vi = w->variation; if (vi < 0) vi = 0; if (vi > 3) vi = 3;
    const wb_var_t *v = &WB_EFFECTS[e].vars[vi];
    if (v->kind == 1) apply_delay(w, v);
    else apply_grain(w, v);
}

void wb_apply_all(wb_t *w) {
    w->ring.frozen = w->hold ? 1 : 0;
    wb_apply_effect(w);
    wb_apply_tone(w);
    wb_apply_space(w);
}

/* generative re-roll — reuses per-voice RNG (scatter/pan re-randomize) and, with more
 * "range", re-rolls the variation and nudges activity/shape within bounds. Stays in the
 * current effect/palette; Evolve fires this on a clock, Dice fires it once. */
void wb_evolve_roll(wb_t *w, int range) {
    for (int v = 0; v < WB_NV; v++)
        w->voices[v].rng ^= (wb_rng_next(&w->evo_rng) | 1u);   /* fresh scatter/pan */
    if (range >= 1) w->variation = (int)(wb_rng_next(&w->evo_rng) & 3u);
    if (range >= 2) {
        w->activity = clampf(w->activity + wb_rng_bi(&w->evo_rng) * 0.10f, 0.05f, 1.0f);
        w->shape    = clampf(w->shape    + wb_rng_bi(&w->evo_rng) * 0.10f, 0.0f,  1.0f);
    }
    w->params_dirty = 1;
}
