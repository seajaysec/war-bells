/* test_stereo_delay.c — the multitap delay now fans a MONO input out to stereo (per-tap pan).
 *
 * Feed a mono source through a full-wet Taps delay (no reverb, no dry) and confirm the output is
 * decorrelated (L != R) — a mono-summed delay would give L==R (decorrelation ~0); the per-tap
 * ping-pong pan gives a clearly stereo image. Plus: stays finite + bounded. */
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
static void fill_noise(int16_t *b, int frames, unsigned *st){           /* dual-mono noise (L==R) */
    for(int i=0;i<frames;i++){ *st=*st*1103515245u+12345u; float s=((int)(*st>>9)/4194304.0f-1.0f)*0.4f;
        b[i*2]=b[i*2+1]=(int16_t)(s*32767.0f); }
}

int main(void){
    memset(&HOST,0,sizeof HOST);
    HOST.api_version=1; HOST.sample_rate=44100; HOST.frames_per_block=128;
    HOST.log=host_log; HOST.get_bpm=host_bpm; HOST.get_clock_status=host_clock;
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(&HOST);
    void *inst = api->create_instance("/tmp",NULL);
    CHECK("create_instance", inst!=NULL);
    if(!inst){ printf("FATAL\n"); return 1; }

    /* full-wet multitap delay, mono input, no reverb/dry — output is the delay alone */
    api->set_param(inst,"effect","Taps"); api->set_param(inst,"variation","0");
    api->set_param(inst,"activity","0.9");        /* engage all 4 taps */
    api->set_param(inst,"repeats","0.6");
    api->set_param(inst,"mix","1");               /* full wet -> no centered dry */
    api->set_param(inst,"effect_vol","1");
    api->set_param(inst,"space","0");             /* no reverb stereo to confound the measurement */
    api->set_param(inst,"input_mode","Mono");     /* force a true mono send */
    api->set_param(inst,"tempo_src","Man");

    unsigned st=7777;
    for(int b=0;b<300;b++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128); }  /* fill the line */

    double sLR=0, sTot=0, maxa=0; int nf=0;
    for(int b=0;b<1200;b++){
        fill_noise(buf,128,&st); api->process_block(inst,buf,128);
        for(int i=0;i<128;i++){ float l=buf[i*2]/32768.0f, r=buf[i*2+1]/32768.0f;
            if(!(l>=-1.5f&&l<=1.5f)||!(r>=-1.5f&&r<=1.5f)) nf++;
            double dl=l-r; sLR += dl*dl; sTot += (double)l*l + (double)r*r;
            double al=fabs(l), ar=fabs(r); if(al>maxa)maxa=al; if(ar>maxa)maxa=ar; }
    }
    double decorr = sTot>1e-9 ? sLR / sTot : 0.0;   /* 0 = mono (L==R); larger = wider */
    printf("  mono-in delay: decorrelation=%.4f  peak=%.3f  nonfinite=%d\n", decorr, maxa, nf);

    CHECK("delay produces a wet signal", sTot > 1e-4);
    CHECK("mono input -> stereo delay (L != R, decorrelation present)", decorr > 0.1);
    CHECK("delay stays finite + bounded", maxa < 1.0 && nf == 0);
    /* (a delay SHOULD ring its tail out on silence — runaway/silence are covered by the stress audit) */

    api->destroy_instance(inst);
    printf("\n%s (%d failures)\n", fails?"TESTS FAILED":"ALL TESTS PASSED", fails);
    return fails?1:0;
}
