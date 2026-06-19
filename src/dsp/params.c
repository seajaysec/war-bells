/* params.c — string param dispatch (set/get), enum option matching, looper
 * transport, patch state, and the Schwung chain metadata (chain_params + ui_hierarchy).
 *
 * Per Schwung conventions (verified against chain_host.c / shadow_ui.js): module.json
 * is a minimal manifest; the host serves chain_params from the plugin first, and
 * ui_hierarchy from the plugin when the manifest has none. A param is only editable if
 * it appears in chain_params with a `type`. Labels/values are kept short to fit the
 * 128px screen (label col ~12 chars, enum value col ~5-6 chars). Variation labels are
 * per-effect (dynamic). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include "wb_internal.h"

/* enum option tables — SHORT, glanceable values (must match chain_params options) */
static const char *EFFECT_OPTS[WB_NEFFECTS] = {
    "Arp","Cutup","Chop","Glide","Seq","Stack",
    "Cloud","Drone","Chain","Taps","Warp"
};
static const char *REVERB_OPTS[4] = {"Room","Dark","Hall","Vast"};
static const char *SHIMMER_OPTS[4] = {"Off","Oct+","Oct-","5th"};
static const char *SUBDIV_OPTS[6] = {"1/4","1/2","1x","2x","4x","8x"};
static const char *ONOFF[2]       = {"Off","On"};
static const char *TEMPOSRC[3]    = {"Free","Sync","Man"};
static const char *GRAINENV[4]    = {"Soft","Pluck","Swell","Gate"};
static const char *PSCALE_OPTS[6] = {"Off","Maj","Min","Pent","Oct","5th"};
static const char *MOT_TGT[7]     = {"Off","Act","Filt","Space","Mix","Mod","Warp"};
static const char *MOT_RATE[7]    = {"8bar","4bar","2bar","1bar","1/2","1/4","1/8"};
static const char *MOT_SHAPE[4]   = {"Sine","Tri","Ramp","Rand"};
static const char *EVORANGE[3]    = {"Soft","Mid","Wild"};
static const char *DICE_OPTS[2]   = {"-","Roll"};
static const char *PRESET_OPTS[19] = {"Init","Arp","Stutr","Chop","Glass","Seq","Stack","Cloud",
                                      "Drone","Birds","Taps","Warp","Sheen","Motn","Evolv","Scale","Bloom","Trails","Spiral"};
static const char *HOLDSTYLE[2]   = {"Latch","Gate"};
static const char *INPUT_OPTS[2]  = {"Ster","Mono"};
static const char *ROUTE_OPTS[2]  = {"Post","Pre"};
static const char *ORDER_OPTS[2]  = {"Std","Dub1"};
static const char *FADE_OPTS[3]   = {"Both","In","Out"};
static const char *BYPASS_OPTS[3] = {"Buf","Trail","True"};
static const char *TRANSPORT_OPTS[9] = {"Idle","Rec","Play","Dub","Stop","Erase","Undo","Save","Load"};
static const char *USER_OP_OPTS[4] = {"Idle","Save","Load","Del"};
static const float BYPASS_LAG[3]  = {0.005f, 0.6f, 0.002f};
#define WB_USER_SLOTS 16

/* chain_params metadata (the editor keys on this). One %s = the current effect's 4
 * variation labels. %% = a literal percent unit. "name" is the on-screen label (short). */
static const char *CHAIN_PARAMS_FMT =
"["
"{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"enum\",\"options\":[\"Init\",\"Arp\",\"Stutr\",\"Chop\",\"Glass\",\"Seq\",\"Stack\",\"Cloud\",\"Drone\",\"Birds\",\"Taps\",\"Warp\",\"Sheen\",\"Motn\",\"Evolv\",\"Scale\",\"Bloom\",\"Trails\",\"Spiral\"]},"
"{\"key\":\"effect\",\"name\":\"Effect\",\"type\":\"enum\",\"options\":[\"Arp\",\"Cutup\",\"Chop\",\"Glide\",\"Seq\",\"Stack\",\"Cloud\",\"Drone\",\"Chain\",\"Taps\",\"Warp\"]},"
"{\"key\":\"variation\",\"name\":\"Var\",\"type\":\"enum\",\"options\":[%s]},"
"{\"key\":\"activity\",\"name\":\"Activity\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"repeats\",\"name\":\"Repeats\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"shape\",\"name\":\"Shape\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"mix\",\"name\":\"Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"effect_vol\",\"name\":\"FX Vol\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"space\",\"name\":\"Space\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"filter\",\"name\":\"Filter\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"filter_res\",\"name\":\"Reso\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"grain_env\",\"name\":\"Grain Env\",\"type\":\"enum\",\"options\":[\"Soft\",\"Pluck\",\"Swell\",\"Gate\"]},"
"{\"key\":\"pitch_scale\",\"name\":\"Scale\",\"type\":\"enum\",\"options\":[\"Off\",\"Maj\",\"Min\",\"Pent\",\"Oct\",\"5th\"]},"
"{\"key\":\"reverse\",\"name\":\"Reverse\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
"{\"key\":\"tempo_src\",\"name\":\"Clock\",\"type\":\"enum\",\"options\":[\"Free\",\"Sync\",\"Man\"]},"
"{\"key\":\"subdiv\",\"name\":\"Subdiv\",\"type\":\"enum\",\"options\":[\"1/4\",\"1/2\",\"1x\",\"2x\",\"4x\",\"8x\"]},"
"{\"key\":\"tempo\",\"name\":\"Tempo\",\"type\":\"int\",\"min\":20,\"max\":300,\"step\":1,\"unit\":\"BPM\"},"
"{\"key\":\"reverb_mode\",\"name\":\"Reverb\",\"type\":\"enum\",\"options\":[\"Room\",\"Dark\",\"Hall\",\"Vast\"]},"
"{\"key\":\"shimmer\",\"name\":\"Shimmer\",\"type\":\"enum\",\"options\":[\"Off\",\"Oct+\",\"Oct-\",\"5th\"]},"
"{\"key\":\"width\",\"name\":\"Width\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"mod_depth\",\"name\":\"Mod Dep\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"mod_rate\",\"name\":\"Mod Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"sustain\",\"name\":\"Sustain\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"warp\",\"name\":\"Warp\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"display_format\":\".2f\"},"
"{\"key\":\"mot_target\",\"name\":\"Mot Dest\",\"type\":\"enum\",\"options\":[\"Off\",\"Act\",\"Filt\",\"Space\",\"Mix\",\"Mod\",\"Warp\"]},"
"{\"key\":\"mot_rate\",\"name\":\"Mot Rate\",\"type\":\"enum\",\"options\":[\"8bar\",\"4bar\",\"2bar\",\"1bar\",\"1/2\",\"1/4\",\"1/8\"]},"
"{\"key\":\"mot_depth\",\"name\":\"Mot Dep\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"mot_shape\",\"name\":\"Wave\",\"type\":\"enum\",\"options\":[\"Sine\",\"Tri\",\"Ramp\",\"Rand\"]},"
"{\"key\":\"evolve\",\"name\":\"Evolve\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"evo_range\",\"name\":\"Range\",\"type\":\"enum\",\"options\":[\"Soft\",\"Mid\",\"Wild\"]},"
"{\"key\":\"dice\",\"name\":\"Dice\",\"type\":\"enum\",\"options\":[\"-\",\"Roll\"]},"
"{\"key\":\"hold\",\"name\":\"Hold\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
"{\"key\":\"hold_style\",\"name\":\"Hold Mode\",\"type\":\"enum\",\"options\":[\"Latch\",\"Gate\"]},"
"{\"key\":\"duck\",\"name\":\"Bloom\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"looper_on\",\"name\":\"Looper\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
"{\"key\":\"transport\",\"name\":\"Transport\",\"type\":\"enum\",\"options\":[\"Idle\",\"Rec\",\"Play\",\"Dub\",\"Stop\",\"Erase\",\"Undo\"]},"
"{\"key\":\"loop_level\",\"name\":\"Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"unit\":\"%%\"},"
"{\"key\":\"loop_speed\",\"name\":\"Speed\",\"type\":\"float\",\"min\":0.25,\"max\":4,\"step\":0.05,\"display_format\":\".2f\"},"
"{\"key\":\"loop_reverse\",\"name\":\"Reverse\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
"{\"key\":\"loop_route\",\"name\":\"Record\",\"type\":\"enum\",\"options\":[\"Post\",\"Pre\"]},"
"{\"key\":\"loop_only\",\"name\":\"Solo\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
"{\"key\":\"input_mode\",\"name\":\"Input\",\"type\":\"enum\",\"options\":[\"Ster\",\"Mono\"]},"
"{\"key\":\"input_gain\",\"name\":\"In Gain\",\"type\":\"float\",\"min\":0,\"max\":2,\"step\":0.05,\"display_format\":\".2f\"},"
"{\"key\":\"bypass\",\"name\":\"Bypass\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
"{\"key\":\"bypass_trails\",\"name\":\"Trails\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
"{\"key\":\"eco\",\"name\":\"Eco CPU\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
"{\"key\":\"user_slot\",\"name\":\"User Slot\",\"type\":\"int\",\"min\":1,\"max\":16,\"step\":1},"
"{\"key\":\"user_op\",\"name\":\"User Op\",\"type\":\"enum\",\"options\":[\"Idle\",\"Save\",\"Load\",\"Del\"]}"
"]";

/* ui_hierarchy — STRUCTURE ONLY (no options here; metadata comes from chain_params).
 * Each key appears in exactly ONE level (chain_host aborts on duplicate keys). Root =
 * 8 knobs + the 8 most-used params + sub-level links. */
static const char *UI_HIERARCHY_JSON =
"{\"levels\":{"
 "\"root\":{\"name\":\"War Bells\",\"knobs\":[\"effect\",\"variation\",\"activity\",\"repeats\",\"shape\",\"mix\",\"space\",\"filter\"],"
   "\"params\":[{\"key\":\"preset\",\"name\":\"Preset\"},{\"key\":\"effect\",\"name\":\"Effect\"},{\"key\":\"variation\",\"name\":\"Var\"},"
   "{\"key\":\"activity\",\"name\":\"Activity\"},{\"key\":\"repeats\",\"name\":\"Repeats\"},"
   "{\"key\":\"shape\",\"name\":\"Shape\"},{\"key\":\"mix\",\"name\":\"Mix\"},"
   "{\"key\":\"space\",\"name\":\"Space\"},{\"key\":\"filter\",\"name\":\"Filter\"},"
   "{\"level\":\"tone\",\"name\":\"Tone\"},{\"level\":\"time\",\"name\":\"Time\"},"
   "{\"level\":\"spacefx\",\"name\":\"Space FX\"},{\"level\":\"motion\",\"name\":\"Motion\"},{\"level\":\"generate\",\"name\":\"Generate\"},{\"level\":\"perform\",\"name\":\"Perform\"},"
   "{\"level\":\"looper\",\"name\":\"Looper\"},"
   "{\"level\":\"user\",\"name\":\"User Slots\"},{\"level\":\"config\",\"name\":\"Config\"}]},"
 "\"user\":{\"name\":\"User Slots\",\"knobs\":[\"user_slot\",\"user_op\"],"
   "\"params\":[{\"key\":\"user_slot\",\"name\":\"User Slot\"},{\"key\":\"user_op\",\"name\":\"User Op\"}]},"
 "\"tone\":{\"name\":\"Tone\",\"knobs\":[\"filter_res\",\"effect_vol\",\"grain_env\",\"pitch_scale\"],"
   "\"params\":[{\"key\":\"filter_res\",\"name\":\"Reso\"},{\"key\":\"effect_vol\",\"name\":\"FX Vol\"},{\"key\":\"grain_env\",\"name\":\"Grain Env\"},{\"key\":\"pitch_scale\",\"name\":\"Scale\"}]},"
 "\"time\":{\"name\":\"Time\",\"knobs\":[\"tempo_src\",\"subdiv\",\"tempo\"],"
   "\"params\":[{\"key\":\"tempo_src\",\"name\":\"Clock\"},{\"key\":\"subdiv\",\"name\":\"Subdiv\"},{\"key\":\"tempo\",\"name\":\"Tempo\"}]},"
 "\"spacefx\":{\"name\":\"Space FX\",\"knobs\":[\"reverb_mode\",\"shimmer\",\"width\",\"sustain\",\"warp\",\"mod_depth\",\"mod_rate\"],"
   "\"params\":[{\"key\":\"reverb_mode\",\"name\":\"Reverb\"},{\"key\":\"shimmer\",\"name\":\"Shimmer\"},{\"key\":\"width\",\"name\":\"Width\"},{\"key\":\"sustain\",\"name\":\"Sustain\"},{\"key\":\"warp\",\"name\":\"Warp\"},{\"key\":\"mod_depth\",\"name\":\"Mod Dep\"},{\"key\":\"mod_rate\",\"name\":\"Mod Rate\"}]},"
 "\"motion\":{\"name\":\"Motion\",\"knobs\":[\"mot_target\",\"mot_rate\",\"mot_depth\",\"mot_shape\"],"
   "\"params\":[{\"key\":\"mot_target\",\"name\":\"Mot Dest\"},{\"key\":\"mot_rate\",\"name\":\"Mot Rate\"},{\"key\":\"mot_depth\",\"name\":\"Mot Dep\"},{\"key\":\"mot_shape\",\"name\":\"Wave\"}]},"
 "\"generate\":{\"name\":\"Generate\",\"knobs\":[\"evolve\",\"evo_range\",\"dice\"],"
   "\"params\":[{\"key\":\"evolve\",\"name\":\"Evolve\"},{\"key\":\"evo_range\",\"name\":\"Range\"},{\"key\":\"dice\",\"name\":\"Dice\"}]},"
 "\"perform\":{\"name\":\"Perform\",\"knobs\":[\"reverse\",\"hold\",\"hold_style\",\"duck\"],"
   "\"params\":[{\"key\":\"reverse\",\"name\":\"Reverse\"},{\"key\":\"hold\",\"name\":\"Hold\"},{\"key\":\"hold_style\",\"name\":\"Hold Mode\"},{\"key\":\"duck\",\"name\":\"Bloom\"}]},"
 "\"looper\":{\"name\":\"Looper\",\"knobs\":[\"looper_on\",\"transport\",\"loop_level\",\"loop_speed\",\"loop_reverse\",\"loop_route\",\"loop_only\"],"
   "\"params\":[{\"key\":\"looper_on\",\"name\":\"Looper\"},{\"key\":\"transport\",\"name\":\"Transport\"},"
   "{\"key\":\"loop_level\",\"name\":\"Level\"},{\"key\":\"loop_speed\",\"name\":\"Speed\"},"
   "{\"key\":\"loop_reverse\",\"name\":\"Reverse\"},{\"key\":\"loop_route\",\"name\":\"Record\"},{\"key\":\"loop_only\",\"name\":\"Solo\"}]},"
 "\"config\":{\"name\":\"Config\",\"knobs\":[\"input_mode\",\"input_gain\",\"bypass\",\"bypass_trails\",\"eco\"],"
   "\"params\":[{\"key\":\"input_mode\",\"name\":\"Input\"},{\"key\":\"input_gain\",\"name\":\"In Gain\"},"
   "{\"key\":\"bypass\",\"name\":\"Bypass\"},{\"key\":\"bypass_trails\",\"name\":\"Trails\"},{\"key\":\"eco\",\"name\":\"Eco CPU\"}]}"
"}}";

/* parse an enum value: accept the option string OR a numeric index */
static int enum_parse(const char *val, const char *const *opts, int n) {
    if (!val) return 0;
    for (int i = 0; i < n; i++) if (strcmp(val, opts[i]) == 0) return i;
    if (val[0] == '-' || (val[0] >= '0' && val[0] <= '9')) {
        int i = atoi(val); if (i < 0) i = 0; if (i >= n) i = n - 1; return i;
    }
    return 0;
}

/* variation is a per-effect enum: match val against the current effect's labels */
static int variation_parse(wb_t *w, const char *val) {
    int e = w->effect; if (e < 0) e = 0; if (e >= WB_NEFFECTS) e = WB_NEFFECTS - 1;
    for (int i = 0; i < 4; i++)
        if (strcmp(val, WB_EFFECTS[e].vars[i].label) == 0) return i;
    if (val[0] == '-' || (val[0] >= '0' && val[0] <= '9')) {
        int i = atoi(val); if (i < 0) i = 0; if (i > 3) i = 3; return i;
    }
    return 0;
}

void wb_params_defaults(wb_t *w) {
    w->effect = 5; w->variation = 0;            /* Stack, oct+ */
    w->activity = 0.3f; w->repeats = 0.4f; w->shape = 0.4f;
    w->filter = 1.0f; w->filter_res = 0.1f;
    w->mix = 0.7f; w->effect_vol = 0.7f;
    w->space = 0.15f; w->reverb_mode = 0;
    w->mod_depth = 0.0f; w->mod_rate = 0.4f;
    w->subdiv = 2; w->tempo_manual = 110.0f; w->tempo_src = 0;
    w->hold = 0; w->hold_style = 0; w->reverse = 0; w->bypass = 0; w->bypass_style = 0; w->eco = 0;
    w->bypass_trails = 0;
    w->input_mono = 0; w->input_gain = 1.0f;
    w->looper_on = 0; w->loop_reverse = 0; w->loop_route = 0;
    w->loop_order = 0; w->loop_quantize = 0; w->loop_only = 0; w->loop_burst = 0;
    w->loop_level = 0.9f; w->loop_fade = 0.1f; w->loop_speed = 1.0f; w->loop_fademode = 0;
    w->grain_env = 0; w->preset = 0; w->tempo_drift = 0.0f;
    w->shimmer = 0; w->pitch_scale = 0;
    w->mot_target = 0; w->mot_rate = 3; w->mot_depth = 0.4f; w->mot_shape = 0;
    w->evolve = 0.0f; w->evo_range = 1;
    w->duck = 0.0f; w->width = 1.0f;
    w->sustain = 0.0f; w->warp = 0.5f; w->warp_eff = 0.5f;
    w->user_slot = 1;
    w->cur_tempo = 110.0f;
}

/* character presets — pick one to jump the effect + macros to a starting point
 * (effect indices: 0 Arp 1 Cutup 2 Chop 3 Glide 4 Seq 5 Stack 6 Cloud 7 Drone
 *  8 Chain 9 Taps 10 Warp). Drawn from creator patches + the manual's feel. */
/* Character presets — a pretty example of each effect + the value-adds (shimmer / scale / motion /
 * evolve). Every field a preset touches is a user-accessible param (hard rule). Each preset first
 * resets macros + value-adds to neutral so it sounds the same regardless of prior state. */
static void apply_preset(wb_t *w, int idx) {
    if (idx < 0) idx = 0;
    if (idx > 18) idx = 18;
    w->preset = idx;
    /* neutral baseline (each case overrides what it needs) */
    w->reverse = 0; w->shimmer = 0; w->pitch_scale = 0;
    w->mot_target = 0; w->mot_rate = 3; w->mot_depth = 0.4f; w->mot_shape = 0;
    w->evolve = 0.0f; w->evo_range = 1; w->width = 1.0f; w->duck = 0.0f; w->hold = 0;
    w->mod_depth = 0.0f; w->mod_rate = 0.40f; w->filter = 1.0f; w->filter_res = 0.1f;
    w->effect_vol = 0.72f; w->grain_env = 0; w->reverb_mode = 0; w->bypass_trails = 0;
    w->sustain = 0.0f; w->warp = 0.5f;
    switch (idx) {
    case 0: /* Init — clean octave stack */
        w->effect=5; w->variation=0; w->activity=0.30f; w->repeats=0.40f; w->shape=0.40f;
        w->mix=0.70f; w->effect_vol=0.70f; w->space=0.15f; break;
    case 1: /* Arp — plucky glitch arpeggios at varied speeds */
        w->effect=0; w->variation=1; w->activity=0.55f; w->repeats=0.40f; w->shape=0.45f;
        w->mix=0.75f; w->space=0.15f; w->grain_env=1; break;
    case 2: /* Stutr — Cutup with filter sweeps + delay */
        w->effect=1; w->variation=2; w->activity=0.60f; w->repeats=0.55f; w->shape=0.50f;
        w->mix=0.85f; w->space=0.20f; w->filter=0.70f; w->filter_res=0.40f; break;
    case 3: /* Chop — pitch-shifted chops, dark room */
        w->effect=2; w->variation=1; w->activity=0.65f; w->repeats=0.45f; w->shape=0.45f;
        w->mix=0.80f; w->space=0.20f; w->reverb_mode=1; w->grain_env=1; break;
    case 4: /* Glass — octave glide, scale-locked to major, hall */
        w->effect=3; w->variation=2; w->activity=0.80f; w->repeats=0.40f; w->shape=0.60f;
        w->mix=1.0f; w->space=0.40f; w->reverb_mode=2; w->pitch_scale=1; break;
    case 5: /* Seq — overlapping filter-swept rhythms */
        w->effect=4; w->variation=2; w->activity=0.70f; w->repeats=0.45f; w->shape=0.40f;
        w->mix=0.85f; w->space=0.30f; w->filter=0.80f; w->filter_res=0.30f; w->reverb_mode=1; break;
    case 6: /* Stack — full half/normal/double/quad octave stack */
        w->effect=5; w->variation=3; w->activity=0.70f; w->repeats=0.40f; w->shape=0.50f;
        w->mix=0.90f; w->space=0.35f; w->reverb_mode=2; w->pitch_scale=4; break;
    case 7: /* Cloud — lush dense grain wash with shimmer */
        w->effect=6; w->variation=1; w->activity=0.65f; w->repeats=0.60f; w->shape=0.50f;
        w->mix=0.85f; w->effect_vol=0.5f; w->space=0.4f; w->reverb_mode=2; w->shimmer=1; break;
    case 8: /* Drone — resonant bandpass drone, slow filter motion, vast space */
        w->effect=7; w->variation=2; w->activity=0.20f; w->repeats=0.85f; w->shape=0.50f;
        w->mix=0.70f; w->space=0.50f; w->reverb_mode=3;
        w->mot_target=2; w->mot_rate=1; w->mot_depth=0.50f; w->mot_shape=0; break;
    case 9: /* Birds — Chain cascades, reverse, plucky */
        w->effect=8; w->variation=3; w->activity=0.75f; w->repeats=0.40f; w->shape=0.40f;
        w->mix=0.80f; w->space=0.25f; w->reverse=1; w->grain_env=1; break;
    case 10: /* Taps — swung multitap delay, dark */
        w->effect=9; w->variation=1; w->activity=0.50f; w->repeats=0.65f; w->shape=0.40f;
        w->mix=0.70f; w->space=0.20f; w->reverb_mode=1; break;
    case 11: /* Warp — pitch-shifted taps, scale-locked, hall */
        w->effect=10; w->variation=2; w->activity=0.55f; w->repeats=0.60f; w->shape=0.50f;
        w->mix=0.85f; w->space=0.40f; w->reverb_mode=2; w->pitch_scale=1; break;
    case 12: /* Sheen — shimmer reverb showcase (octave-up bloom) */
        w->effect=5; w->variation=0; w->activity=0.50f; w->repeats=0.50f; w->shape=0.55f;
        w->mix=1.0f; w->space=0.75f; w->reverb_mode=2; w->shimmer=1; w->grain_env=2; break;
    case 13: /* Motn — Motion LFO sweeping the filter on a grain cloud */
        w->effect=6; w->variation=0; w->activity=0.60f; w->repeats=0.55f; w->shape=0.50f;
        w->mix=0.85f; w->space=0.55f; w->reverb_mode=2;
        w->mot_target=2; w->mot_rate=3; w->mot_depth=0.70f; w->mot_shape=0; break;
    case 14: /* Evolv — self-evolving cascading texture */
        w->effect=8; w->variation=2; w->activity=0.65f; w->repeats=0.55f; w->shape=0.45f;
        w->mix=0.85f; w->space=0.45f; w->reverb_mode=2; w->evolve=0.60f; w->evo_range=1; break;
    case 15: /* Scale — scale-locked octave stack (major) */
        w->effect=5; w->variation=3; w->activity=0.65f; w->repeats=0.45f; w->shape=0.50f;
        w->mix=0.90f; w->space=0.40f; w->reverb_mode=2; w->pitch_scale=1; break;
    case 16: /* Bloom — wet swells in the gaps (duck/bloom showcase) */
        w->effect=6; w->variation=0; w->activity=0.50f; w->repeats=0.55f; w->shape=0.50f;
        w->mix=0.90f; w->space=0.60f; w->reverb_mode=2; w->duck=0.65f; break;
    case 17: /* Trails — lush sustain that rings out when you tap bypass (Avalanche-style) */
        w->effect=5; w->variation=0; w->activity=0.45f; w->repeats=0.85f; w->shape=0.55f;
        w->mix=0.7f; w->effect_vol=0.45f; w->space=0.55f; w->reverb_mode=2; w->shimmer=1; w->sustain=0.25f; w->bypass_trails=1; break;
    case 18: /* Spiral — feedback builds (Sustain) + Motion warps the delay time -> re-pitching ladder */
        w->effect=5; w->variation=0; w->activity=0.40f; w->repeats=0.70f; w->shape=0.55f;
        w->mix=0.85f; w->space=0.75f; w->reverb_mode=3; w->sustain=0.7f;
        w->mot_target=6; w->mot_rate=2; w->mot_depth=0.35f; w->mot_shape=0; break;   /* Motion -> Warp */
    }
    w->params_dirty = 1;
    wb_apply_all(w);
}

/* ---- looper transport ---- */
/* Lazily allocate the ~21MB looper buffers the first time the looper is actually used. Called
 * only from set_param handlers (transport REC / user load) which run on the control thread, NEVER
 * from process_block — so this malloc is realtime-safe. Idle instances never pay the RAM. */
static int wb_loop_ensure(wb_t *w) {
    if (w->lp_bl) return 1;
    int lcap = (int)(WB_LOOP_SEC * WB_SR);
    w->lp_bl = (int16_t*)calloc(lcap, sizeof(int16_t));
    w->lp_br = (int16_t*)calloc(lcap, sizeof(int16_t));
    w->lp_ol = (int16_t*)calloc(lcap, sizeof(int16_t));
    w->lp_or = (int16_t*)calloc(lcap, sizeof(int16_t));
    if (!w->lp_bl || !w->lp_br || !w->lp_ol || !w->lp_or) {
        free(w->lp_bl); free(w->lp_br); free(w->lp_ol); free(w->lp_or);
        w->lp_bl = w->lp_br = w->lp_ol = w->lp_or = NULL;
        return 0;
    }
    wb_looper_attach(&w->looper, w->lp_bl, w->lp_br, w->lp_ol, w->lp_or, lcap);
    return 1;
}
static void looper_close(wb_t *w) {
    int frames = w->looper.writepos;
    if (w->loop_quantize) {
        float bf = WB_SR * 60.0f / (w->cur_tempo > 1.0f ? w->cur_tempo : 120.0f);
        int n = (int)floorf((float)frames / bf + 0.5f); if (n < 1) n = 1;
        frames = (int)(n * bf);
    }
    wb_looper_close(&w->looper, frames);
    w->looper.dir = w->loop_reverse ? -1 : 1;
}
static void loop_path(wb_t *w, char *buf, int len) {
    if (w->module_dir[0]) snprintf(buf, len, "%s/war_bells_loop.wav", w->module_dir);
    else snprintf(buf, len, "/data/UserData/war_bells_loop.wav");
}
static void transport(wb_t *w, int cmd) {
    char path[576];
    switch (cmd) {
        case WB_T_REC:
            if (!wb_loop_ensure(w)) break;   /* allocate buffers on first record */
            if (w->loop_burst && w->looper.frames > 0) wb_looper_clear(&w->looper);
            if (w->looper.state == WB_LP_IDLE && w->looper.frames == 0)
                wb_looper_rec_start(&w->looper);
            break;
        case WB_T_PLAY:
            if (w->looper.state == WB_LP_REC) {
                if (w->loop_order == 1 && !w->loop_burst) { looper_close(w); w->looper.state = WB_LP_DUB; }
                else looper_close(w);
            } else if (w->looper.frames > 0) {
                w->looper.ph = 0.0; w->looper.state = WB_LP_PLAY;
            }
            break;
        case WB_T_DUB:
            if (w->loop_burst) break;
            if (w->looper.state == WB_LP_PLAY) w->looper.state = WB_LP_DUB;
            else if (w->looper.state == WB_LP_DUB) w->looper.state = WB_LP_PLAY;
            break;
        case WB_T_STOP:  w->looper.state = WB_LP_IDLE; break;
        case WB_T_ERASE: wb_looper_clear(&w->looper); break;
        case WB_T_UNDO:  wb_looper_undo(&w->looper); break;
        case WB_T_SAVE:  loop_path(w,path,sizeof(path)); wb_looper_save(&w->looper, path); break;
        case WB_T_LOAD:  if (!wb_loop_ensure(w)) break; loop_path(w,path,sizeof(path));
                         wb_looper_load(&w->looper, path); w->looper_on = 1; break;
        default: break;
    }
}

/* simple JSON float extraction for state restore */
static int json_f(const char *json, const char *key, float *out) {
    char s[48]; snprintf(s, sizeof(s), "\"%s\":", key);
    const char *p = strstr(json, s); if (!p) return -1;
    p += strlen(s); while (*p==' '||*p=='\t') p++;
    *out = (float)atof(p); return 0;
}

/* ---- user-preset bank: params (state JSON) + loop audio per slot ---- */
static void user_dir(wb_t *w, char *buf, int len) {
    if (w->module_dir[0]) snprintf(buf, len, "%s/presets", w->module_dir);
    else                  snprintf(buf, len, "/data/UserData/war_bells_presets");
}
static void user_path(wb_t *w, int slot, const char *ext, char *buf, int len) {
    char d[560]; user_dir(w, d, sizeof d);
    snprintf(buf, len, "%s/slot_%02d.%s", d, slot, ext);
}
static void user_save(wb_t *w) {
    char d[560], jp[640], wp[640], js[2048];
    user_dir(w, d, sizeof d); mkdir(d, 0755);
    int n = wb_params_get(w, "state", js, sizeof js);
    if (n > 0 && n < (int)sizeof js) {
        user_path(w, w->user_slot, "json", jp, sizeof jp);
        FILE *f = fopen(jp, "wb");
        if (f) { fwrite(js, 1, (size_t)n, f); fclose(f); }
    }
    user_path(w, w->user_slot, "wav", wp, sizeof wp);
    wb_looper_save(&w->looper, wp);   /* returns -1 (no-op) if no loop recorded */
}
static void user_load(wb_t *w) {
    char jp[640], wp[640], js[2048];
    user_path(w, w->user_slot, "json", jp, sizeof jp);
    FILE *f = fopen(jp, "rb");
    if (f) {
        size_t n = fread(js, 1, sizeof(js) - 1, f); fclose(f);
        js[n] = '\0';
        if (n) wb_params_set(w, "state", js);
    }
    user_path(w, w->user_slot, "wav", wp, sizeof wp);
    wb_loop_ensure(w);                                          /* need buffers to load into */
    if (wb_looper_load(&w->looper, wp) == 0) w->looper_on = 1;  /* loop present -> arm */
}
static void user_clear(wb_t *w) {
    char jp[640], wp[640];
    user_path(w, w->user_slot, "json", jp, sizeof jp); remove(jp);
    user_path(w, w->user_slot, "wav",  wp, sizeof wp); remove(wp);
}

void wb_params_set(wb_t *w, const char *key, const char *val) {
    if (!w || !key || !val) return;

    if (strcmp(key, "state") == 0) {
        float v;
        if (json_f(val,"effect",&v)==0) w->effect=(int)v;
        if (json_f(val,"variation",&v)==0) w->variation=(int)v;
        if (json_f(val,"activity",&v)==0) w->activity=v;
        if (json_f(val,"repeats",&v)==0) w->repeats=v;
        if (json_f(val,"shape",&v)==0) w->shape=v;
        if (json_f(val,"filter",&v)==0) w->filter=v;
        if (json_f(val,"filter_res",&v)==0) w->filter_res=v;
        if (json_f(val,"mix",&v)==0) w->mix=v;
        if (json_f(val,"effect_vol",&v)==0) w->effect_vol=v;
        if (json_f(val,"space",&v)==0) w->space=v;
        if (json_f(val,"reverb_mode",&v)==0) w->reverb_mode=(int)v;
        if (json_f(val,"mod_depth",&v)==0) w->mod_depth=v;
        if (json_f(val,"mod_rate",&v)==0) w->mod_rate=v;
        if (json_f(val,"subdiv",&v)==0) w->subdiv=(int)v;
        if (json_f(val,"tempo",&v)==0) w->tempo_manual=v;
        if (json_f(val,"tempo_src",&v)==0) w->tempo_src=(int)v;
        if (json_f(val,"reverse",&v)==0) w->reverse=(int)v;
        if (json_f(val,"input_gain",&v)==0) w->input_gain=v;
        if (json_f(val,"loop_level",&v)==0) w->loop_level=v;
        if (json_f(val,"loop_speed",&v)==0) w->loop_speed=v;
        if (json_f(val,"grain_env",&v)==0) w->grain_env=(int)v;
        if (json_f(val,"shimmer",&v)==0) w->shimmer=(int)v;
        if (json_f(val,"pitch_scale",&v)==0) w->pitch_scale=(int)v;
        if (json_f(val,"mot_target",&v)==0) w->mot_target=(int)v;
        if (json_f(val,"mot_rate",&v)==0) w->mot_rate=(int)v;
        if (json_f(val,"mot_depth",&v)==0) w->mot_depth=v;
        if (json_f(val,"mot_shape",&v)==0) w->mot_shape=(int)v;
        if (json_f(val,"evolve",&v)==0) w->evolve=v;
        if (json_f(val,"evo_range",&v)==0) w->evo_range=(int)v;
        if (json_f(val,"duck",&v)==0) w->duck=v;
        if (json_f(val,"width",&v)==0) w->width=v;
        if (json_f(val,"sustain",&v)==0) w->sustain=v;
        if (json_f(val,"warp",&v)==0) w->warp=v;
        if (json_f(val,"preset",&v)==0) w->preset=(int)v;
        if (json_f(val,"hold",&v)==0) w->hold=(int)v;
        if (json_f(val,"hold_style",&v)==0) w->hold_style=(int)v;
        if (json_f(val,"looper_on",&v)==0) w->looper_on=(int)v;
        if (json_f(val,"loop_reverse",&v)==0) w->loop_reverse=(int)v;
        if (json_f(val,"loop_fade",&v)==0) w->loop_fade=v;
        if (json_f(val,"loop_fademode",&v)==0) w->loop_fademode=(int)v;
        if (json_f(val,"loop_route",&v)==0) w->loop_route=(int)v;
        if (json_f(val,"loop_order",&v)==0) w->loop_order=(int)v;
        if (json_f(val,"loop_quantize",&v)==0) w->loop_quantize=(int)v;
        if (json_f(val,"loop_only",&v)==0) w->loop_only=(int)v;
        if (json_f(val,"loop_burst",&v)==0) w->loop_burst=(int)v;
        if (json_f(val,"input_mode",&v)==0) w->input_mono=(int)v;
        if (json_f(val,"bypass",&v)==0) w->bypass=(int)v;
        if (json_f(val,"bypass_style",&v)==0) w->bypass_style=(int)v;
        if (json_f(val,"eco",&v)==0) w->eco=(int)v;
        if (json_f(val,"bypass_trails",&v)==0) w->bypass_trails=(int)v;
        /* re-apply derived state (these are set by their own handlers normally) */
        w->looper.fade = 0.005f + w->loop_fade * 4.0f;
        w->looper.fademode = w->loop_fademode;
        w->looper.dir = w->loop_reverse ? -1 : 1;
        w->looper.level = w->loop_level;
        w->looper.speed = w->loop_speed;
        w->looper.only = w->loop_only;
        if (w->bypass_style < 0 || w->bypass_style > 2) w->bypass_style = 0;
        w->bypass_lag = BYPASS_LAG[w->bypass_style];
        wb_apply_all(w);   /* recompute effect/tone/space + ring.frozen from hold */
        return;
    }

    if (strcmp(key,"preset")==0)        { apply_preset(w, enum_parse(val,PRESET_OPTS,19)); }
    else if (strcmp(key,"effect")==0)   { w->effect = enum_parse(val,EFFECT_OPTS,WB_NEFFECTS); w->params_dirty=1; }
    else if (strcmp(key,"variation")==0){ w->variation = variation_parse(w,val); w->params_dirty=1; }
    else if (strcmp(key,"activity")==0) { w->activity = wb_clampf((float)atof(val),0,1); w->params_dirty=1; }
    else if (strcmp(key,"repeats")==0)  { w->repeats = wb_clampf((float)atof(val),0,1); w->params_dirty=1; }
    else if (strcmp(key,"shape")==0)    { w->shape = wb_clampf((float)atof(val),0,1); w->params_dirty=1; }
    else if (strcmp(key,"filter")==0)   { w->filter = wb_clampf((float)atof(val),0,1); wb_apply_tone(w); }
    else if (strcmp(key,"filter_res")==0){ w->filter_res = wb_clampf((float)atof(val),0,1); wb_apply_tone(w); }
    else if (strcmp(key,"grain_env")==0){ w->grain_env = enum_parse(val,GRAINENV,4); w->params_dirty=1; }
    else if (strcmp(key,"pitch_scale")==0){ w->pitch_scale = enum_parse(val,PSCALE_OPTS,6); w->params_dirty=1; }
    else if (strcmp(key,"mix")==0)      { w->mix = wb_clampf((float)atof(val),0,1); }
    else if (strcmp(key,"effect_vol")==0){ w->effect_vol = wb_clampf((float)atof(val),0,1); }
    else if (strcmp(key,"space")==0)    { w->space = wb_clampf((float)atof(val),0,1); wb_apply_space(w); }
    else if (strcmp(key,"reverb_mode")==0){ w->reverb_mode = enum_parse(val,REVERB_OPTS,4); wb_apply_space(w); }
    else if (strcmp(key,"shimmer")==0)  { w->shimmer = enum_parse(val,SHIMMER_OPTS,4); }
    else if (strcmp(key,"width")==0)    { w->width = wb_clampf((float)atof(val),0,1); wb_apply_space(w); }
    else if (strcmp(key,"sustain")==0)  { w->sustain = wb_clampf((float)atof(val),0,1); }
    else if (strcmp(key,"warp")==0)     { w->warp = wb_clampf((float)atof(val),0,1); }
    else if (strcmp(key,"duck")==0)     { w->duck = wb_clampf((float)atof(val),0,1); }
    else if (strcmp(key,"mod_depth")==0){ w->mod_depth = wb_clampf((float)atof(val),0,1); wb_apply_tone(w); }
    else if (strcmp(key,"mod_rate")==0) { w->mod_rate = wb_clampf((float)atof(val),0,1); wb_apply_tone(w); }
    else if (strcmp(key,"mot_target")==0){ w->mot_target = enum_parse(val,MOT_TGT,7); }
    else if (strcmp(key,"mot_rate")==0) { w->mot_rate = enum_parse(val,MOT_RATE,7); }
    else if (strcmp(key,"mot_depth")==0){ w->mot_depth = wb_clampf((float)atof(val),0,1); }
    else if (strcmp(key,"mot_shape")==0){ w->mot_shape = enum_parse(val,MOT_SHAPE,4); }
    else if (strcmp(key,"evolve")==0)   { w->evolve = wb_clampf((float)atof(val),0,1); }
    else if (strcmp(key,"evo_range")==0){ w->evo_range = enum_parse(val,EVORANGE,3); }
    else if (strcmp(key,"dice")==0)     { if (enum_parse(val,DICE_OPTS,2)==1) wb_evolve_roll(w,2); }
    else if (strcmp(key,"subdiv")==0)   { w->subdiv = enum_parse(val,SUBDIV_OPTS,6); w->params_dirty=1; }
    else if (strcmp(key,"tempo_src")==0){ w->tempo_src = enum_parse(val,TEMPOSRC,3); w->params_dirty=1; }
    else if (strcmp(key,"tempo")==0)    { w->tempo_manual = wb_clampf((float)atof(val),20,300); w->params_dirty=1; }
    else if (strcmp(key,"hold")==0)     {
        int on = enum_parse(val,ONOFF,2);
        if (!(w->looper_on && on)) { w->hold = on; w->ring.frozen = on; }
    }
    else if (strcmp(key,"hold_style")==0){ w->hold_style = enum_parse(val,HOLDSTYLE,2); }
    else if (strcmp(key,"reverse")==0)  { w->reverse = enum_parse(val,ONOFF,2); w->params_dirty=1; }
    else if (strcmp(key,"bypass")==0)   { w->bypass = enum_parse(val,ONOFF,2); }
    else if (strcmp(key,"bypass_style")==0){ w->bypass_style = enum_parse(val,BYPASS_OPTS,3); w->bypass_lag = BYPASS_LAG[w->bypass_style]; }
    else if (strcmp(key,"eco")==0)      { w->eco = enum_parse(val,ONOFF,2); wb_apply_space(w); w->params_dirty=1; }
    else if (strcmp(key,"bypass_trails")==0){ w->bypass_trails = enum_parse(val,ONOFF,2); }
    else if (strcmp(key,"input_mode")==0){ w->input_mono = enum_parse(val,INPUT_OPTS,2); }
    else if (strcmp(key,"input_gain")==0){ w->input_gain = wb_clampf((float)atof(val),0,2); }
    else if (strcmp(key,"looper_on")==0){
        w->looper_on = enum_parse(val,ONOFF,2);
        if (w->looper_on) { if (w->hold) { w->hold=0; w->ring.frozen=0; } }
        else wb_looper_clear(&w->looper);
    }
    else if (strcmp(key,"transport")==0){ transport(w, enum_parse(val,TRANSPORT_OPTS,9)); }
    else if (strcmp(key,"loop_reverse")==0){ w->loop_reverse = enum_parse(val,ONOFF,2); w->looper.dir = w->loop_reverse?-1:1; }
    else if (strcmp(key,"loop_level")==0){ w->loop_level = wb_clampf((float)atof(val),0,1); w->looper.level = w->loop_level; }
    else if (strcmp(key,"loop_fade")==0){ w->loop_fade = wb_clampf((float)atof(val),0,1); w->looper.fade = 0.005f + w->loop_fade*4.0f; }
    else if (strcmp(key,"loop_fademode")==0){ w->loop_fademode = enum_parse(val,FADE_OPTS,3); w->looper.fademode = w->loop_fademode; }
    else if (strcmp(key,"loop_speed")==0){ w->loop_speed = wb_clampf((float)atof(val),0.25f,4.0f); w->looper.speed = w->loop_speed; }
    else if (strcmp(key,"loop_route")==0){ w->loop_route = enum_parse(val,ROUTE_OPTS,2); }
    else if (strcmp(key,"loop_order")==0){ w->loop_order = enum_parse(val,ORDER_OPTS,2); }
    else if (strcmp(key,"loop_quantize")==0){ w->loop_quantize = enum_parse(val,ONOFF,2); }
    else if (strcmp(key,"loop_only")==0){ w->loop_only = enum_parse(val,ONOFF,2); w->looper.only = w->loop_only; }
    else if (strcmp(key,"loop_burst")==0){ w->loop_burst = enum_parse(val,ONOFF,2); }
    else if (strcmp(key,"user_slot")==0){ int s=atoi(val); if(s<1)s=1; if(s>WB_USER_SLOTS)s=WB_USER_SLOTS; w->user_slot=s; }
    else if (strcmp(key,"user_op")==0){ int op=enum_parse(val,USER_OP_OPTS,4);
        if (op==1) user_save(w); else if (op==2) user_load(w); else if (op==3) user_clear(w); }
}

int wb_params_get(wb_t *w, const char *key, char *buf, int buf_len) {
    if (!w || !key || !buf) return -1;
    int e = w->effect; if (e < 0) e = 0; if (e >= WB_NEFFECTS) e = WB_NEFFECTS - 1;
    int vi = w->variation; if (vi < 0) vi = 0; if (vi > 3) vi = 3;

    if (strcmp(key,"name")==0)        return snprintf(buf,buf_len,"War Bells");
    if (strcmp(key,"effect")==0)      return snprintf(buf,buf_len,"%s",EFFECT_OPTS[e]);
    if (strcmp(key,"variation")==0)   return snprintf(buf,buf_len,"%s",WB_EFFECTS[e].vars[vi].label);
    if (strcmp(key,"activity")==0)    return snprintf(buf,buf_len,"%.3f",w->activity);
    if (strcmp(key,"repeats")==0)     return snprintf(buf,buf_len,"%.3f",w->repeats);
    if (strcmp(key,"shape")==0)       return snprintf(buf,buf_len,"%.3f",w->shape);
    if (strcmp(key,"filter")==0)      return snprintf(buf,buf_len,"%.3f",w->filter);
    if (strcmp(key,"filter_res")==0)  return snprintf(buf,buf_len,"%.3f",w->filter_res);
    if (strcmp(key,"mix")==0)         return snprintf(buf,buf_len,"%.3f",w->mix);
    if (strcmp(key,"effect_vol")==0)  return snprintf(buf,buf_len,"%.3f",w->effect_vol);
    if (strcmp(key,"space")==0)       return snprintf(buf,buf_len,"%.3f",w->space);
    if (strcmp(key,"reverb_mode")==0) return snprintf(buf,buf_len,"%s",REVERB_OPTS[w->reverb_mode]);
    if (strcmp(key,"shimmer")==0)     return snprintf(buf,buf_len,"%s",SHIMMER_OPTS[w->shimmer]);
    if (strcmp(key,"width")==0)       return snprintf(buf,buf_len,"%.3f",w->width);
    if (strcmp(key,"sustain")==0)     return snprintf(buf,buf_len,"%.3f",w->sustain);
    if (strcmp(key,"warp")==0)        return snprintf(buf,buf_len,"%.3f",w->warp);
    if (strcmp(key,"duck")==0)        return snprintf(buf,buf_len,"%.3f",w->duck);
    if (strcmp(key,"mod_depth")==0)   return snprintf(buf,buf_len,"%.3f",w->mod_depth);
    if (strcmp(key,"mod_rate")==0)    return snprintf(buf,buf_len,"%.3f",w->mod_rate);
    if (strcmp(key,"mot_target")==0)  return snprintf(buf,buf_len,"%s",MOT_TGT[w->mot_target]);
    if (strcmp(key,"mot_rate")==0)    return snprintf(buf,buf_len,"%s",MOT_RATE[w->mot_rate]);
    if (strcmp(key,"mot_depth")==0)   return snprintf(buf,buf_len,"%.3f",w->mot_depth);
    if (strcmp(key,"mot_shape")==0)   return snprintf(buf,buf_len,"%s",MOT_SHAPE[w->mot_shape]);
    if (strcmp(key,"evolve")==0)      return snprintf(buf,buf_len,"%.3f",w->evolve);
    if (strcmp(key,"evo_range")==0)   return snprintf(buf,buf_len,"%s",EVORANGE[w->evo_range]);
    if (strcmp(key,"dice")==0)        return snprintf(buf,buf_len,"-");  /* momentary */
    if (strcmp(key,"subdiv")==0)      return snprintf(buf,buf_len,"%s",SUBDIV_OPTS[w->subdiv]);
    if (strcmp(key,"preset")==0)      return snprintf(buf,buf_len,"%s",PRESET_OPTS[w->preset]);
    if (strcmp(key,"grain_env")==0)   return snprintf(buf,buf_len,"%s",GRAINENV[w->grain_env]);
    if (strcmp(key,"pitch_scale")==0) return snprintf(buf,buf_len,"%s",PSCALE_OPTS[w->pitch_scale]);
    if (strcmp(key,"tempo_src")==0)   return snprintf(buf,buf_len,"%s",TEMPOSRC[w->tempo_src]);
    if (strcmp(key,"tempo")==0)       return snprintf(buf,buf_len,"%.0f",w->tempo_manual);
    if (strcmp(key,"hold")==0)        return snprintf(buf,buf_len,"%s",ONOFF[w->hold]);
    if (strcmp(key,"hold_style")==0)  return snprintf(buf,buf_len,"%s",HOLDSTYLE[w->hold_style]);
    if (strcmp(key,"reverse")==0)     return snprintf(buf,buf_len,"%s",ONOFF[w->reverse]);
    if (strcmp(key,"bypass")==0)      return snprintf(buf,buf_len,"%s",ONOFF[w->bypass]);
    if (strcmp(key,"eco")==0)         return snprintf(buf,buf_len,"%s",ONOFF[w->eco]);
    if (strcmp(key,"bypass_trails")==0)return snprintf(buf,buf_len,"%s",ONOFF[w->bypass_trails]);
    if (strcmp(key,"bypass_style")==0)return snprintf(buf,buf_len,"%s",BYPASS_OPTS[w->bypass_style]);
    if (strcmp(key,"loop_burst")==0)  return snprintf(buf,buf_len,"%s",ONOFF[w->loop_burst]);
    if (strcmp(key,"input_mode")==0)  return snprintf(buf,buf_len,"%s",INPUT_OPTS[w->input_mono]);
    if (strcmp(key,"input_gain")==0)  return snprintf(buf,buf_len,"%.3f",w->input_gain);
    if (strcmp(key,"looper_on")==0)   return snprintf(buf,buf_len,"%s",ONOFF[w->looper_on]);
    if (strcmp(key,"transport")==0)   return snprintf(buf,buf_len,"%s",TRANSPORT_OPTS[w->looper.state]);
    if (strcmp(key,"loop_reverse")==0)return snprintf(buf,buf_len,"%s",ONOFF[w->loop_reverse]);
    if (strcmp(key,"loop_level")==0)  return snprintf(buf,buf_len,"%.3f",w->loop_level);
    if (strcmp(key,"loop_fade")==0)   return snprintf(buf,buf_len,"%.3f",w->loop_fade);
    if (strcmp(key,"loop_fademode")==0)return snprintf(buf,buf_len,"%s",FADE_OPTS[w->loop_fademode]);
    if (strcmp(key,"loop_speed")==0)  return snprintf(buf,buf_len,"%.3f",w->loop_speed);
    if (strcmp(key,"loop_route")==0)  return snprintf(buf,buf_len,"%s",ROUTE_OPTS[w->loop_route]);
    if (strcmp(key,"loop_order")==0)  return snprintf(buf,buf_len,"%s",ORDER_OPTS[w->loop_order]);
    if (strcmp(key,"loop_quantize")==0)return snprintf(buf,buf_len,"%s",ONOFF[w->loop_quantize]);
    if (strcmp(key,"loop_only")==0)   return snprintf(buf,buf_len,"%s",ONOFF[w->loop_only]);
    if (strcmp(key,"user_slot")==0)   return snprintf(buf,buf_len,"%d",w->user_slot);
    if (strcmp(key,"user_op")==0)     return snprintf(buf,buf_len,"Idle");  /* momentary */

    if (strcmp(key,"state")==0) {
        return snprintf(buf,buf_len,
          "{\"effect\":%d,\"variation\":%d,\"activity\":%.4f,\"repeats\":%.4f,\"shape\":%.4f,"
          "\"filter\":%.4f,\"filter_res\":%.4f,\"mix\":%.4f,\"effect_vol\":%.4f,\"space\":%.4f,"
          "\"reverb_mode\":%d,\"mod_depth\":%.4f,\"mod_rate\":%.4f,\"subdiv\":%d,\"tempo\":%.1f,"
          "\"tempo_src\":%d,\"reverse\":%d,\"input_gain\":%.4f,\"loop_level\":%.4f,\"loop_speed\":%.4f,"
          "\"grain_env\":%d,\"preset\":%d,\"hold\":%d,\"hold_style\":%d,\"looper_on\":%d,"
          "\"loop_reverse\":%d,\"loop_fade\":%.4f,\"loop_fademode\":%d,\"loop_route\":%d,\"loop_order\":%d,"
          "\"loop_quantize\":%d,\"loop_only\":%d,\"loop_burst\":%d,\"input_mode\":%d,\"bypass\":%d,\"bypass_style\":%d,"
          "\"shimmer\":%d,\"pitch_scale\":%d,\"mot_target\":%d,\"mot_rate\":%d,\"mot_depth\":%.4f,\"mot_shape\":%d,"
          "\"evolve\":%.4f,\"evo_range\":%d,\"duck\":%.4f,\"width\":%.4f,\"eco\":%d,\"bypass_trails\":%d,"
          "\"sustain\":%.4f,\"warp\":%.4f}",
          w->effect,w->variation,w->activity,w->repeats,w->shape,w->filter,w->filter_res,w->mix,
          w->effect_vol,w->space,w->reverb_mode,w->mod_depth,w->mod_rate,w->subdiv,w->tempo_manual,
          w->tempo_src,w->reverse,w->input_gain,w->loop_level,w->loop_speed,
          w->grain_env,w->preset,w->hold,w->hold_style,w->looper_on,
          w->loop_reverse,w->loop_fade,w->loop_fademode,w->loop_route,w->loop_order,
          w->loop_quantize,w->loop_only,w->loop_burst,w->input_mono,w->bypass,w->bypass_style,
          w->shimmer,w->pitch_scale,w->mot_target,w->mot_rate,w->mot_depth,w->mot_shape,
          w->evolve,w->evo_range,w->duck,w->width,w->eco,w->bypass_trails,w->sustain,w->warp);
    }
    if (strcmp(key,"chain_params")==0) {
        char vo[128];
        snprintf(vo, sizeof(vo), "\"%s\",\"%s\",\"%s\",\"%s\"",
            WB_EFFECTS[e].vars[0].label, WB_EFFECTS[e].vars[1].label,
            WB_EFFECTS[e].vars[2].label, WB_EFFECTS[e].vars[3].label);
        int n = snprintf(buf, buf_len, CHAIN_PARAMS_FMT, vo);
        return (n > 0 && n < buf_len) ? n : -1;
    }
    if (strcmp(key,"ui_hierarchy")==0) {
        int n = (int)strlen(UI_HIERARCHY_JSON);
        if (n < buf_len) { strcpy(buf, UI_HIERARCHY_JSON); return n; }
        return -1;
    }
    return -1;
}
