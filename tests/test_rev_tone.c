/* test_rev_tone.c — functional test for the reverb-send Tone control (rev_tone).
 *
 * The stress matrix never sweeps rev_tone (it stays neutral there), so this test
 * exercises the active paths directly. Strategy: feed CLEAN dry noise into a wet
 * reverb (mix=0 + mod_depth=0 => no grains/chorus => a fully deterministic tail),
 * excite the tail, then measure low-band and high-band energy of the decay at
 * three settings: 0.0 (full high-pass), 0.5 (flat), 1.0 (full low-pass).
 *
 * Asserts: HP cuts the lows, LP cuts the highs, neutral keeps both, and every
 * setting stays finite + bounded (no runaway). Plus the param-plumbing checks
 * (rev_tone in chain_params/ui_hierarchy; shape moved off root into the Tone page). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"

extern audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

static void host_log(const char *m) { (void)m; }
static float host_bpm(void) { return 120.0f; }
static int   host_clock(void) { return 1; }

static int fails = 0;
#define CHECK(desc, cond) do { \
    printf("%s  %s\n", (cond)?"PASS":"FAIL", desc); if(!(cond)) fails++; } while(0)

static host_api_v1_t HOST;
static int16_t buf[128*2];

static void fill_noise(int16_t *b, int frames, unsigned *st) {
    for (int i=0;i<frames;i++){
        *st = *st*1103515245u+12345u; float s=((int)(*st>>9)/4194304.0f-1.0f)*0.4f;
        b[i*2]=(int16_t)(s*32767.0f); b[i*2+1]=(int16_t)(s*32767.0f);
    }
}

/* Excite the reverb with a fixed noise burst, then measure the (silent-input) tail.
 * lowE = energy below ~200 Hz, highE = energy above ~4 kHz, maxabs = worst sample. */
typedef struct { double lowE, highE; double maxabs; int nonfinite; } band_t;
static band_t measure_tail(audio_fx_api_v2_t *api, void *inst, float rev_tone) {
    char v[16]; snprintf(v,sizeof v,"%.3f",rev_tone);
    api->set_param(inst,"rev_tone",v);
    /* excite ~0.7 s with a fixed seed so every setting sees identical input */
    unsigned st=4242u;
    for(int blk=0;blk<240;blk++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128); }
    /* measure ~1.2 s of tail on silent input */
    const float aL=0.0284f;   /* 1-pole LP ~200 Hz @44.1k */
    const float aH=0.435f;    /* 1-pole LP ~4 kHz; highband = x - lp4k */
    double lpL=0, lp4=0, lowSS=0, highSS=0, maxa=0; int nf=0; long n=0;
    for(int blk=0;blk<420;blk++){
        memset(buf,0,sizeof(buf)); api->process_block(inst,buf,128);
        for(int i=0;i<128;i++){
            float x = buf[i*2]/32768.0f;            /* left channel */
            if(!(x>=-1.5f && x<=1.5f)) nf++;
            float ax = x<0?-x:x; if(ax>maxa) maxa=ax;
            lpL += aL*(x-lpL);  lowSS  += lpL*lpL;
            lp4 += aH*(x-lp4);  float hi=x-lp4; highSS += hi*hi;
            n++;
        }
    }
    band_t b; b.lowE=sqrt(lowSS/n); b.highE=sqrt(highSS/n); b.maxabs=maxa; b.nonfinite=nf;
    return b;
}

int main(void){
    memset(&HOST,0,sizeof(HOST));
    HOST.api_version=1; HOST.sample_rate=44100; HOST.frames_per_block=128;
    HOST.log=host_log; HOST.get_bpm=host_bpm; HOST.get_clock_status=host_clock;

    audio_fx_api_v2_t *api = move_audio_fx_init_v2(&HOST);
    void *inst = api->create_instance("/tmp",NULL);
    CHECK("create_instance", inst!=NULL);
    if(!inst){ printf("FATAL\n"); return 1; }

    /* clean wet reverb: dry-only source (mix=0, mod=0 -> deterministic), big bright room */
    api->set_param(inst,"preset","Init");
    api->set_param(inst,"mix","0");          /* mute grains -> reverb is fed clean dry */
    api->set_param(inst,"mod_depth","0");     /* no chorus */
    api->set_param(inst,"shimmer","Off");
    api->set_param(inst,"sustain","0");
    api->set_param(inst,"space","0.85");
    api->set_param(inst,"reverb_mode","Vast"); /* size 0.95, low damping -> bright, long, low-end prone */
    api->set_param(inst,"filter","1");         /* global LP fully open -> doesn't mask the test */
    api->set_param(inst,"tempo_src","Man");    /* no Free-mode drift */

    band_t flat = measure_tail(api, inst, 0.5f);
    band_t hp   = measure_tail(api, inst, 0.0f);
    band_t lp   = measure_tail(api, inst, 1.0f);
    printf("  flat: low=%.5f high=%.5f peak=%.3f\n", flat.lowE, flat.highE, flat.maxabs);
    printf("  HP  : low=%.5f high=%.5f peak=%.3f\n", hp.lowE,   hp.highE,   hp.maxabs);
    printf("  LP  : low=%.5f high=%.5f peak=%.3f\n", lp.lowE,   lp.highE,   lp.maxabs);

    /* sanity: the flat tail actually has energy in both bands (else the test proves nothing) */
    CHECK("flat tail has low + high energy", flat.lowE>1e-4 && flat.highE>1e-4);
    /* high-pass (rev_tone=0) cuts the low end of the reverb */
    CHECK("HP setting reduces low-end vs flat", hp.lowE < flat.lowE*0.7);
    /* it should leave the highs largely intact (not a wholesale level drop) */
    CHECK("HP setting keeps the highs", hp.highE > flat.highE*0.6);
    /* low-pass (rev_tone=1) cuts the high end of the reverb */
    CHECK("LP setting reduces high-end vs flat", lp.highE < flat.highE*0.7);
    /* nothing runs away or goes non-finite in any setting */
    CHECK("all settings stay bounded (<1.0) + finite",
          flat.maxabs<1.0 && hp.maxabs<1.0 && lp.maxabs<1.0 &&
          flat.nonfinite==0 && hp.nonfinite==0 && lp.nonfinite==0);

    /* param plumbing: rev_tone is a typed float; default neutral 0.5 */
    api->set_param(inst,"preset","Init");
    char sb[32]; api->get_param(inst,"rev_tone",sb,sizeof sb);
    CHECK("rev_tone defaults to 0.5 (neutral)", fabs(atof(sb)-0.5)<1e-3);
    char big[8192]; api->get_param(inst,"chain_params",big,sizeof big);
    CHECK("chain_params exposes rev_tone (typed float)",
          strstr(big,"\"key\":\"rev_tone\",\"name\":\"Rv Tone\",\"type\":\"float\"")!=NULL);
    char hier[8192]; api->get_param(inst,"ui_hierarchy",hier,sizeof hier);
    /* rev_tone promoted to a root knob; shape demoted to the Tone page */
    char *root = strstr(hier,"\"root\""); char *tone = strstr(hier,"\"tone\"");
    char *sh   = strstr(hier,"\"shape\"");   /* exact key; won't match "mot_shape" */
    CHECK("ui_hierarchy: root knobs include rev_tone",
          root && tone && strstr(root,"rev_tone")!=NULL && strstr(root,"rev_tone") < tone);
    /* shape's only occurrence must be in/after the Tone level (i.e. not in root, which precedes tone) */
    CHECK("ui_hierarchy: shape moved out of root onto the Tone page",
          sh && tone && sh > tone && strstr(tone,"\"shape\"")!=NULL);

    /* state round-trips rev_tone (presets / user slots persist it) */
    api->set_param(inst,"rev_tone","0.2");
    char st1[2048]; api->get_param(inst,"state",st1,sizeof st1);
    CHECK("state JSON carries rev_tone", strstr(st1,"\"rev_tone\":")!=NULL);
    api->set_param(inst,"rev_tone","0.9");
    api->set_param(inst,"state",st1);                 /* restore the 0.2 snapshot */
    api->get_param(inst,"rev_tone",sb,sizeof sb);
    CHECK("state restore brings rev_tone back", fabs(atof(sb)-0.2)<1e-3);

    /* MIDI CC 33 drives rev_tone (root-knob parity) */
    if (api->on_midi){
        uint8_t cc[3]={0xB0,33,127}; api->on_midi(inst,cc,3,0);
        api->get_param(inst,"rev_tone",sb,sizeof sb);
        CHECK("MIDI CC33 -> rev_tone", atof(sb)>0.9);
    }

    api->destroy_instance(inst);
    printf("\n%s (%d failures)\n", fails?"TESTS FAILED":"ALL TESTS PASSED", fails);
    return fails?1:0;
}
