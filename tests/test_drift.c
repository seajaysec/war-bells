/* test_drift.c — functional test for Drift (per-voice independent slow LFOs).
 *
 * Drift gives each of the 6 grain voices its own slow LFOs on gain/pan/rate, so the cloud
 * breathes continuously. We can't see the internal LFOs, so we measure the OUTPUT: with Drift
 * on, the slow level envelope (block-RMS smoothed over ~1 s) wanders much more than with it off,
 * and the stereo balance moves — while staying finite + bounded. Drift=0 must be a true no-op.
 *
 * Determinism: same instance, same seeded input stream; we compare a Drift-off window against a
 * Drift-on window. Tempo=Manual + Evolve=0 so nothing else introduces slow movement. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"

extern audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

static void host_log(const char *m){ (void)m; }
static float host_bpm(void){ return 120.0f; }
static int   host_clock(void){ return 1; }

static int fails = 0;
#define CHECK(d,c) do{ printf("%s  %s\n",(c)?"PASS":"FAIL",d); if(!(c)) fails++; }while(0)

static host_api_v1_t HOST;
static int16_t buf[128*2];
static void fill_noise(int16_t *b, int frames, unsigned *st){
    for(int i=0;i<frames;i++){ *st=*st*1103515245u+12345u; float s=((int)(*st>>9)/4194304.0f-1.0f)*0.4f;
        b[i*2]=(int16_t)(s*32767.0f); b[i*2+1]=(int16_t)(s*32767.0f); }
}

/* Process ~`sec` seconds, tracking a ~1 s-smoothed level envelope (range = slow breathing) and the
 * slow L-R balance swing. Returns envelope range, balance range, worst sample, nonfinite count. */
typedef struct { double env_range, bal_range, maxabs; int nonfinite; } move_t;
static move_t measure_movement(audio_fx_api_v2_t *api, void *inst, unsigned *st, double sec){
    int nblk = (int)(sec * 44100.0 / 128.0);
    const float a = 0.003f;                 /* 1-pole ~1 s on the block-RMS envelope */
    double env=0, bal=0; int seeded=0;
    double env_lo=1e9, env_hi=-1e9, bal_lo=1e9, bal_hi=-1e9, maxa=0; int nf=0;
    for(int b=0;b<nblk;b++){
        fill_noise(buf,128,st); api->process_block(inst,buf,128);
        double sL=0,sR=0,mx=0;
        for(int i=0;i<128;i++){ float l=buf[i*2]/32768.0f, r=buf[i*2+1]/32768.0f;
            if(!(l>=-1.5f&&l<=1.5f)||!(r>=-1.5f&&r<=1.5f)) nf++;
            sL+=l*l; sR+=r*r; double al=fabs(l),ar=fabs(r); if(al>mx)mx=al; if(ar>mx)mx=ar; }
        double rms=sqrt((sL+sR)/256.0), balance=sqrt(sL/128.0)-sqrt(sR/128.0);
        if(mx>maxa) maxa=mx;
        if(!seeded){ env=rms; bal=balance; seeded=1; }
        env += a*(rms-env);  bal += a*(balance-bal);
        if(b>nblk/4){    /* let the smoother settle before tracking range */
            if(env<env_lo)env_lo=env; if(env>env_hi)env_hi=env;
            if(bal<bal_lo)bal_lo=bal; if(bal>bal_hi)bal_hi=bal;
        }
    }
    move_t m; m.env_range=env_hi-env_lo; m.bal_range=bal_hi-bal_lo; m.maxabs=maxa; m.nonfinite=nf;
    return m;
}

int main(void){
    memset(&HOST,0,sizeof HOST);
    HOST.api_version=1; HOST.sample_rate=44100; HOST.frames_per_block=128;
    HOST.log=host_log; HOST.get_bpm=host_bpm; HOST.get_clock_status=host_clock;
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(&HOST);
    void *inst = api->create_instance("/tmp",NULL);
    CHECK("create_instance", inst!=NULL);
    if(!inst){ printf("FATAL\n"); return 1; }

    /* a busy grain cloud, isolated (no reverb), deterministic clock */
    api->set_param(inst,"effect","Cloud"); api->set_param(inst,"activity","0.8");
    api->set_param(inst,"mix","1"); api->set_param(inst,"effect_vol","0.9");
    api->set_param(inst,"space","0"); api->set_param(inst,"tempo_src","Man");
    api->set_param(inst,"evolve","0"); api->set_param(inst,"mot_target","Off");

    unsigned st=9001;
    for(int b=0;b<400;b++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128); } /* settle */

    api->set_param(inst,"drift","0");
    move_t off = measure_movement(api, inst, &st, 18.0);
    api->set_param(inst,"drift","1");
    for(int b=0;b<400;b++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128); } /* let LFOs ramp in */
    move_t on  = measure_movement(api, inst, &st, 18.0);

    printf("  drift OFF: env_range=%.5f bal_range=%.5f peak=%.3f\n", off.env_range, off.bal_range, off.maxabs);
    printf("  drift ON : env_range=%.5f bal_range=%.5f peak=%.3f\n", on.env_range,  on.bal_range,  on.maxabs);

    CHECK("Drift adds slow level breathing (env range up)",  on.env_range > off.env_range*1.3);
    CHECK("Drift adds stereo movement (balance range up)",   on.bal_range > off.bal_range*1.3);
    CHECK("both stay finite + bounded (<1.0)",
          off.maxabs<1.0 && on.maxabs<1.0 && off.nonfinite==0 && on.nonfinite==0);

    /* param plumbing: default neutral, typed float, on the Generate page, state round-trips */
    api->set_param(inst,"preset","Init");
    char sb[32]; api->get_param(inst,"drift",sb,sizeof sb);
    CHECK("drift defaults to 0 (off)", fabs(atof(sb))<1e-3);
    char big[8192]; api->get_param(inst,"chain_params",big,sizeof big);
    CHECK("chain_params exposes drift (typed float)",
          strstr(big,"\"key\":\"drift\",\"name\":\"Drift\",\"type\":\"float\"")!=NULL);
    char hier[8192]; api->get_param(inst,"ui_hierarchy",hier,sizeof hier);
    char *gen = strstr(hier,"\"generate\"");
    CHECK("drift is on the Generate page", gen && strstr(gen,"drift")!=NULL);
    api->set_param(inst,"drift","0.4");
    char state[2048]; api->get_param(inst,"state",state,sizeof state);
    CHECK("state JSON carries drift", strstr(state,"\"drift\":")!=NULL);
    api->set_param(inst,"drift","0.9"); api->set_param(inst,"state",state);
    api->get_param(inst,"drift",sb,sizeof sb);
    CHECK("state restore brings drift back", fabs(atof(sb)-0.4)<1e-3);

    api->destroy_instance(inst);
    printf("\n%s (%d failures)\n", fails?"TESTS FAILED":"ALL TESTS PASSED", fails);
    return fails?1:0;
}
