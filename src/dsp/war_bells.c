/* war_bells.c — War Bells audio_fx plugin: entry point, instance lifecycle, the
 * per-sample process_block signal chain, and MIDI onset capture.
 *
 * Signal path (ported from the norns engine):
 *   in -> input gain/mono -> capture ring (frozen by Hold)
 *      -> 6 grain voices + multitap delay -> wet
 *   MIX(dry,wet) -> chorus(pitch-mod) -> SPACE(delay+reverb) -> resonant LP
 *      -> looper (thru + base/overdub) -> bypass xfade -> out  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
#include "wb_internal.h"

static const host_api_v1_t *g_host = NULL;

static void wb_log(const char *m) {
    if (g_host && g_host->log) { char b[160]; snprintf(b,sizeof(b),"[war_bells] %s",m); g_host->log(b); }
}

/* ---- lifecycle ---- */
static void *v2_create(const char *module_dir, const char *config_json) {
    (void)config_json;
    wb_t *w = (wb_t*)calloc(1, sizeof(wb_t));
    if (!w) return NULL;
    w->host = g_host;
    if (module_dir) { strncpy(w->module_dir, module_dir, sizeof(w->module_dir)-1); }

    int cap   = (int)(WB_CAP_SEC   * WB_SR);
    int dlen  = (int)(WB_DELAY_SEC * WB_SR);
    int lcap  = (int)(WB_LOOP_SEC  * WB_SR);

    w->cap_l = (float*)calloc(cap, sizeof(float));
    w->cap_r = (float*)calloc(cap, sizeof(float));
    w->dl_l  = (float*)calloc(dlen, sizeof(float));
    w->dl_r  = (float*)calloc(dlen, sizeof(float));
    /* looper buffers (~21 MB) are allocated lazily on first arm — an idle War Bells costs 0 MB
     * for the looper, which matters when several instances are loaded. See wb_loop_ensure(). */
    w->lp_bl = w->lp_br = w->lp_ol = w->lp_or = NULL;
    if (!w->cap_l||!w->cap_r||!w->dl_l||!w->dl_r) {
        wb_log("alloc failed");
        free(w->cap_l);free(w->cap_r);free(w->dl_l);free(w->dl_r);free(w);
        return NULL;
    }

    wb_tang_init(); wb_sin_init();   /* shared, idempotent: SVF tan-g + LFO sine LUTs */
    wb_ring_init(&w->ring, w->cap_l, w->cap_r, cap);
    for (int i = 0; i < WB_NV; i++) wb_voice_init(&w->voices[i], 0x1000u + i*2654435761u);
    wb_delay_init(&w->delay, w->dl_l, w->dl_r, dlen);
    wb_chorus_init(&w->chorus);
    wb_reverb_init(&w->reverb);
    wb_svf_reset(&w->filt_l); wb_svf_reset(&w->filt_r);
    wb_looper_init(&w->looper, w->lp_bl, w->lp_br, w->lp_ol, w->lp_or, lcap);
    wb_transient_init(&w->trans);
    wb_pshift_init(&w->shimM); w->shim_m = 0.0f;
    w->mot_rng = 0x2BAD51E5u; w->mot_phase = 0.0; w->mot_newrand = 1;
    w->evo_rng = 0x9E3779B1u; w->evo_acc = 0.0;
    w->bypass_lag = 0.005f;
    w->space_dtime = 0.25f; w->space_fb = 0.3f;
    w->drift_rng = 0x51ED77u;

    wb_params_defaults(w);
    wb_apply_all(w);
    wb_log("instance created");
    return w;
}

static void v2_destroy(void *inst) {
    wb_t *w = (wb_t*)inst; if (!w) return;
    free(w->cap_l);free(w->cap_r);free(w->dl_l);free(w->dl_r);
    free(w->lp_bl);free(w->lp_br);free(w->lp_ol);free(w->lp_or);
    free(w);
}

/* feedback-comb space delay, stereo, with interpolated read */
static inline void wb_space_delay(wb_t *w, float inL, float inR, float *outL, float *outR) {
    float baseDly = wb_clampf(w->space_dtime, 0.02f, 1.0f) * WB_SR;

    /* Neutral (no Sustain, no Warp) -> the exact original delay (existing presets unchanged). */
    if (w->sustain < 1e-4f && w->warp_eff > 0.4999f && w->warp_eff < 0.5001f) {
        float pos = (float)w->spdl_w - baseDly;
        while (pos < 0.0f) pos += WB_SPDL_LEN;
        int i0 = (int)pos; int i1 = i0 + 1; if (i1 >= WB_SPDL_LEN) i1 = 0;
        float f = pos - (float)i0;
        float sdL = wb_lerpf(w->spdl_l[i0], w->spdl_l[i1], f);
        float sdR = wb_lerpf(w->spdl_r[i0], w->spdl_r[i1], f);
        w->spdl_l[w->spdl_w] = wb_softclip(inL + sdL * w->space_fb);
        w->spdl_r[w->spdl_w] = wb_softclip(inR + sdR * w->space_fb);
        w->spdl_smooth = (double)baseDly;
        if (++w->spdl_w >= WB_SPDL_LEN) w->spdl_w = 0;
        *outL = sdL; *outR = sdR; return;
    }

    /* Tape/feedback engine. Warp scales the delay TIME (+/-1 octave); the read offset is slewed, so
     * the rate of change IS the pitch shift (rho = 1 - D'). Sustain pushes feedback toward unity for
     * sound-on-sound build, kept safe by a loop tone-LPF (more damping as Sustain rises), a DC
     * blocker, and the softclip limiter. */
    float warpRatio = exp2f((w->warp_eff - 0.5f) * 2.0f);          /* 0.5->1x, 0->0.25x, 1->4x time */
    float targetDly = wb_clampf(baseDly * warpRatio, 64.0f, (float)(WB_SPDL_LEN - 4));
    w->spdl_smooth += ((double)targetDly - w->spdl_smooth) * 0.0015; /* slew -> liquid pitch bend */
    float pos = (float)w->spdl_w - (float)w->spdl_smooth;
    while (pos < 0.0f) pos += WB_SPDL_LEN;
    while (pos >= WB_SPDL_LEN) pos -= WB_SPDL_LEN;
    int i0 = (int)pos; int i1 = i0 + 1; if (i1 >= WB_SPDL_LEN) i1 = 0;
    float f = pos - (float)i0;
    float sdL = wb_lerpf(w->spdl_l[i0], w->spdl_l[i1], f);
    float sdR = wb_lerpf(w->spdl_r[i0], w->spdl_r[i1], f);

    float g = wb_lerpf(w->space_fb, 0.992f, w->sustain);          /* feedback toward unity */
    float toneCoef = wb_lerpf(1.0f, 0.45f, w->sustain);           /* loop HF damping rises with Sustain */
    w->sp_lp_l += (sdL - w->sp_lp_l) * toneCoef; float fbL = w->sp_lp_l;
    w->sp_lp_r += (sdR - w->sp_lp_r) * toneCoef; float fbR = w->sp_lp_r;
    float wrL = wb_softclip(inL + fbL * g);
    float wrR = wb_softclip(inR + fbR * g);
    /* DC blocker (one-pole HPF ~5 Hz) — mandatory once feedback nears unity */
    float yL = wrL - w->sp_dcx_l + 0.9995f * w->sp_dcy_l; w->sp_dcx_l = wrL; w->sp_dcy_l = yL;
    float yR = wrR - w->sp_dcx_r + 0.9995f * w->sp_dcy_r; w->sp_dcx_r = wrR; w->sp_dcy_r = yR;
    w->spdl_l[w->spdl_w] = yL;
    w->spdl_r[w->spdl_w] = yR;
    if (++w->spdl_w >= WB_SPDL_LEN) w->spdl_w = 0;
    *outL = sdL; *outR = sdR;
}

static void v2_process(void *inst, int16_t *audio, int frames) {
    wb_t *w = (wb_t*)inst; if (!w) return;
    wb_flush_denormals();   /* denormal stalls in the reverb/delay tails = CPU spikes; flush on the audio thread */

    /* tempo: 0 Free (internal + organic drift), 1 Sync (host clock), 2 Manual (fixed) */
    float base = w->tempo_manual;
    if (w->tempo_src == 1 && w->host && w->host->get_bpm) base = w->host->get_bpm();
    float bpm = base;
    if (w->tempo_src == 0) {
        /* bounded random walk with gentle pull to center -> "predictably unpredictable" */
        w->tempo_drift += wb_rng_bi(&w->drift_rng) * 0.0009f - w->tempo_drift * 0.0006f;
        if (w->tempo_drift >  0.06f) w->tempo_drift =  0.06f;
        if (w->tempo_drift < -0.06f) w->tempo_drift = -0.06f;
        bpm = base * (1.0f + w->tempo_drift);
    } else {
        w->tempo_drift = 0.0f;
    }
    if (fabsf(bpm - w->cur_tempo) > 0.4f) { w->cur_tempo = bpm; w->params_dirty = 1; }
    /* Evolve: generative re-roll on a tempo-synced clock (faster as evolve -> 1) */
    if (w->evolve > 1e-3f) {
        float beat = 60.0f / (w->cur_tempo > 1.0f ? w->cur_tempo : 120.0f);
        float interval = wb_lerpf(4.0f, 0.25f, w->evolve) * beat;   /* beats between rolls */
        w->evo_acc += (double)frames / (double)WB_SR;
        if (w->evo_acc >= (double)interval) { w->evo_acc = 0.0; wb_evolve_roll(w, w->evo_range); }
    }
    if (w->params_dirty) { wb_apply_effect(w); wb_apply_space(w); w->params_dirty = 0; }

    const wb_var_t *var = &WB_EFFECTS[w->effect].vars[w->variation];
    int onset_driven = var->onset;
    int midi_on = w->midi_onset; w->midi_onset = 0;
    float bcoeff = 1.0f / (w->bypass_lag * WB_SR + 1.0f);
    float msqrt = sqrtf(w->mix), dsqrt = sqrtf(1.0f - w->mix);

    /* Motion: one tempo-synced LFO modulating a chosen macro (computed per block) */
    float spaceEff = w->space;
    w->warp_eff = w->warp;                 /* tape Warp = knob, unless Motion targets it (below) */
    if (w->mot_target > 0 && w->mot_depth > 1e-4f) {
        float beat = 60.0f / (w->cur_tempo > 1.0f ? w->cur_tempo : 120.0f);
        static const float DIV_BEATS[7] = { 32,16,8,4,2,1,0.5f }; /* 8/4/2/1bar,1/2,1/4,1/8 */
        float period = DIV_BEATS[w->mot_rate % 7] * beat; if (period < 0.02f) period = 0.02f;
        w->mot_phase += (double)frames / (double)WB_SR / (double)period;
        if (w->mot_phase >= 1.0) { w->mot_phase -= floor(w->mot_phase); w->mot_newrand = 1; }
        double ph = w->mot_phase; float lfo;
        switch (w->mot_shape) {
            case 1: lfo = (ph<0.5)?(float)(ph*4.0-1.0):(float)(3.0-ph*4.0); break; /* Tri */
            case 2: lfo = (float)(ph*2.0-1.0); break;                              /* Ramp */
            case 3: if (w->mot_newrand){ w->mot_rng_val = wb_rng_bi(&w->mot_rng); w->mot_newrand=0; }
                    lfo = w->mot_rng_val; break;                                   /* Rand S&H */
            default: lfo = sinf((float)(2.0*M_PI*ph)); break;                      /* Sine */
        }
        float a = lfo * w->mot_depth * 0.5f;
        switch (w->mot_target) {
            case 1: { float b=w->activity; w->activity=wb_clampf(b+a,0,1);
                      wb_apply_effect(w); w->activity=b; } break;                  /* Act */
            case 2: { float fe=wb_clampf(w->filter+a,0,1);
                      float fc=wb_lerpf(120.0f,19000.0f,powf(fe,1.6f));
                      wb_svf_set(&w->filt_l,fc,w->fres); wb_svf_set(&w->filt_r,fc,w->fres); } break; /* Filt */
            case 3: spaceEff = wb_clampf(w->space+a,0,1); break;                   /* Space */
            case 4: { float me=wb_clampf(w->mix+a,0,1); msqrt=sqrtf(me); dsqrt=sqrtf(1.0f-me); } break; /* Mix */
            case 5: w->chorus.depth = wb_clampf(w->mod_depth+a,0,1); break;        /* Mod */
            case 6: w->warp_eff = wb_clampf(w->warp+a,0,1); break;                 /* Warp (tape time) */
        }
    }

    for (int i = 0; i < frames; i++) {
        float inL = wb_i16_to_f(audio[i*2])   * w->input_gain;
        float inR = wb_i16_to_f(audio[i*2+1]) * w->input_gain;
        if (w->input_mono) { float m = (inL+inR)*0.5f; inL = m; inR = m; }
        /* bypass ramp computed first: Trails fades the effect's INPUT (so the delay/reverb/grain
         * tails decay naturally, length set by Repeats/Space) while clean dry passes; hard bypass
         * keeps the feed and crossfades the OUTPUT to dry (cutting the tail). */
        float btgt = w->bypass ? 1.0f : 0.0f;
        w->bypass_cur += (btgt - w->bypass_cur) * bcoeff;
        float bc = w->bypass_cur;
        float feed = w->bypass_trails ? (1.0f - bc) : 1.0f;
        float eInL = inL * feed, eInR = inR * feed;
        float mono = (eInL + eInR) * 0.5f;

        int onset = wb_transient_process(&w->trans, mono) || (i == 0 && midi_on);
        if (onset && onset_driven && !w->hold) {
            for (int v = 0; v < WB_NV; v++) w->voices[v].ph = (double)w->voices[v].window;
        }

        wb_ring_write(&w->ring, eInL, eInR);

        float wetL = 0.0f, wetR = 0.0f;
        for (int v = 0; v < WB_NV; v++) wb_voice_process(&w->voices[v], &w->ring, &wetL, &wetR);
        float dL, dR; wb_delay_process(&w->delay, eInL, eInR, &dL, &dR);
        wetL += dL; wetR += dR;

        /* Looper Only mutes the effect (grains/delay) but keeps dry + space + filter + loop */
        float wetGain = w->loop_only ? 0.0f : 1.0f;
        /* Duck: sidechain the wet to the input level so the effect blooms in the gaps */
        if (w->duck > 1e-4f)
            wetGain *= 1.0f - w->duck * wb_clampf(w->trans.env_slow * 6.0f, 0.0f, 1.0f);
        float sigL = eInL * dsqrt + wetL * w->effect_vol * msqrt * wetGain;
        float sigR = eInR * dsqrt + wetR * w->effect_vol * msqrt * wetGain;

        wb_chorus_process(&w->chorus, sigL, sigR, &sigL, &sigR);

        if (spaceEff > 1e-4f) {
            float sdL, sdR; wb_space_delay(w, sigL, sigR, &sdL, &sdR);
            float rvL, rvR;
            if (w->shimmer) {
                /* shimmer: ONE mono pitch-shifter on the reverb tail, fed back with a hard-bounded
                 * gain (denormal-flushed in wb_pshift_process). Mono + LUT window keeps the per-sample
                 * cost low so it stays solid at high grain load. */
                static const float SHIM_RATIO[4] = { 1.0f, 2.0f, 0.5f, 1.5f }; /* -,oct+,oct-,5th */
                float rt = SHIM_RATIO[w->shimmer & 3];
                /* reverb SEND headroom (0.5): continuous input otherwise integrates the room +
                 * shimmer feedback up to the rails over seconds (runaway). Shimmer regen lowered
                 * 0.45->0.3 so its loop settles to a steady state below the ceiling, not at it. */
                float fb = w->shim_m * 0.18f;
                wb_reverb_process(&w->reverb, (sigL + sdL*0.5f)*0.5f + fb, (sigR + sdR*0.5f)*0.5f + fb, &rvL, &rvR);
                float sh = wb_pshift_process(&w->shimM, 0.5f*(rvL+rvR), rt);
                w->shim_m = wb_softclip(sh);   /* soft-saturate the feedback (no hard-clamp fold = no added aliasing) */
            } else {
                wb_reverb_process(&w->reverb, (sigL + sdL*0.5f)*0.5f, (sigR + sdR*0.5f)*0.5f, &rvL, &rvR);
                w->shim_m = 0.0f;
            }
            /* gain-staging: delay + reverb are summed, so scale the pair (equally — preserves their
             * balance/texture) to keep the space bus under unity instead of compounding to ~2x. The
             * Space knob makes up the level; the limiter is left as a rare safety, not a crutch. */
            float spL = (sdL + rvL) * 0.6f, spR = (sdR + rvR) * 0.6f;
            sigL = wb_lerpf(sigL, spL, spaceEff);
            sigR = wb_lerpf(sigR, spR, spaceEff);
        }

        sigL = wb_svf_lp(&w->filt_l, sigL);
        sigR = wb_svf_lp(&w->filt_r, sigR);

        float recL = w->loop_route ? inL : sigL;
        float recR = w->loop_route ? inR : sigR;
        float outL, outR;
        int hit = wb_looper_process(&w->looper, sigL, sigR, recL, recR, &outL, &outR);
        if (hit) wb_looper_close(&w->looper, w->looper.cap);

        /* master makeup on the PROCESSED signal (restores loudness lost to the staging headroom).
         * Applied BEFORE the bypass crossfade so true bypass stays unity (not 1.35x dry). */
        outL *= 1.35f; outR *= 1.35f;

        /* bypass output: Trails = effect tail (already decaying via faded feed) + clean dry rising;
         * hard = crossfade straight to dry (tail cut). */
        if (w->bypass_trails) { outL = outL + inL * bc; outR = outR + inR * bc; }
        else { outL = wb_lerpf(outL, inL, bc); outR = wb_lerpf(outR, inR, bc); }

        audio[i*2]   = wb_f_to_i16(wb_tape_limit(&w->tlim_l, outL));   /* warm tape ceiling */
        audio[i*2+1] = wb_f_to_i16(wb_tape_limit(&w->tlim_r, outR));
    }
}

static void v2_set(void *inst, const char *key, const char *val) { wb_params_set((wb_t*)inst, key, val); }
static int  v2_get(void *inst, const char *key, char *buf, int len) { return wb_params_get((wb_t*)inst, key, buf, len); }

/* MIDI CC map (see docs/DESIGN.md). Values are
 * pushed through the normal param setter so behavior matches the menus exactly. */
static void midi_cc(wb_t *w, int cc, int v) {
    char b[24];
    #define CONT(k)  do{ snprintf(b,sizeof(b),"%.4f", v/127.0f); wb_params_set(w,k,b); }while(0)
    #define TOG(k)   do{ wb_params_set(w,k, v>=64?"1":"0"); }while(0)
    #define IDX(k,i) do{ snprintf(b,sizeof(b),"%d",(i)); wb_params_set(w,k,b); }while(0)
    switch (cc) {
        case 5:  IDX("subdiv", v>5?5:v); break;
        case 6:  CONT("activity"); break;
        case 7:  CONT("shape"); break;
        case 8:  CONT("filter"); break;
        case 9:  CONT("mix"); break;
        case 10: IDX("tempo", (int)(20 + v/127.0f*280)); break;
        case 11: CONT("repeats"); break;
        case 12: CONT("space"); break;
        case 13: CONT("loop_level"); break;
        case 14: CONT("mod_rate"); break;
        case 15: CONT("filter_res"); break;
        case 16: CONT("effect_vol"); break;
        case 17: { snprintf(b,sizeof(b),"%.3f",0.25f+v/127.0f*3.75f); wb_params_set(w,"loop_speed",b);} break;
        case 18: { const float st[6]={0.25f,0.5f,1,2,4,4}; snprintf(b,sizeof(b),"%.3f",st[v>5?5:v]); wb_params_set(w,"loop_speed",b);} break;
        case 19: CONT("mod_depth"); break;
        case 20: IDX("reverb_mode", v/32>3?3:v/32); break;
        case 21: CONT("loop_fade"); break;
        case 22: TOG("looper_on"); break;
        case 23: TOG("loop_reverse"); break;
        case 24: TOG("loop_route"); break;
        case 25: TOG("loop_only"); break;
        case 26: TOG("loop_burst"); break;
        case 27: TOG("loop_quantize"); break;
        case 28: if(v>0) wb_params_set(w,"transport","Rec"); break;
        case 29: if(v>0) wb_params_set(w,"transport","Play"); break;
        case 30: if(v>0) wb_params_set(w,"transport","Dub"); break;
        case 31: if(v>0) wb_params_set(w,"transport","Stop"); break;
        case 34: if(v>0) wb_params_set(w,"transport","Erase"); break;
        case 35: if(v>0) wb_params_set(w,"transport","Undo"); break;
        case 46: if(v>0) wb_params_set(w,"transport","Save"); break;
        case 47: TOG("reverse"); break;
        case 48: TOG("hold"); break;
        case 102: wb_params_set(w,"bypass", v<64?"1":"0"); break;
        default: break;
    }
    #undef CONT
    #undef TOG
    #undef IDX
}

static void v2_midi(void *inst, const uint8_t *msg, int len, int source) {
    (void)source;
    wb_t *w = (wb_t*)inst; if (!w || len < 2) return;
    int status = msg[0] & 0xF0;
    if (status == 0x90 && len >= 3 && msg[2] > 0) w->midi_onset = 1;   /* note-on -> onset */
    else if (status == 0xB0 && len >= 3) midi_cc(w, msg[1], msg[2]);   /* control change */
    else if (status == 0xC0) {                                          /* program change */
        int pc = msg[1];                       /* 0-based; PC#1 == 0 */
        if (pc < 44) { char b[8];
            snprintf(b,sizeof(b),"%d", pc/4); wb_params_set(w,"effect",b);
            snprintf(b,sizeof(b),"%d", pc%4); wb_params_set(w,"variation",b);
        }
    }
}

/* ---- entry point ---- */
static audio_fx_api_v2_t g_api;

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    memset(&g_api, 0, sizeof(g_api));
    g_api.api_version     = AUDIO_FX_API_VERSION_2;
    g_api.create_instance = v2_create;
    g_api.destroy_instance= v2_destroy;
    g_api.process_block   = v2_process;
    g_api.set_param       = v2_set;
    g_api.get_param       = v2_get;
    g_api.on_midi         = v2_midi;
    wb_log("War Bells v2 initialized");
    return &g_api;
}
