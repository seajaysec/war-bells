/* test_stereo_width.c — the global Stereo (M/S) width control.
 *
 * Feed a STEREO source (independent L/R) through a dry path and measure the Side energy
 * (sum (L-R)^2 / total) as Stereo sweeps: 0 collapses to mono (Side ~0), 0.5 is neutral
 * (Side preserved), 1.0 widens (Side boosted). Plus: bounded/finite and the param plumbing. */
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
static void fill_stereo_noise(int16_t *b, int frames, unsigned *sl, unsigned *sr){  /* independent L/R */
    for(int i=0;i<frames;i++){
        *sl=*sl*1103515245u+12345u; *sr=*sr*1664525u+1013904223u;
        float l=((int)(*sl>>9)/4194304.0f-1.0f)*0.4f, r=((int)(*sr>>9)/4194304.0f-1.0f)*0.4f;
        b[i*2]=(int16_t)(l*32767.0f); b[i*2+1]=(int16_t)(r*32767.0f);
    }
}

/* returns Side ratio = sum((L-R)^2) / sum(L^2+R^2); also worst peak + nonfinite */
static double side_ratio(audio_fx_api_v2_t *api, void *inst, const char *stereo, double *maxa, int *nf){
    char v[16]; snprintf(v,sizeof v,"%s",stereo); api->set_param(inst,"stereo",v);
    unsigned sl=11, sr=22;
    for(int b=0;b<60;b++){ fill_stereo_noise(buf,128,&sl,&sr); api->process_block(inst,buf,128); } /* settle */
    double sS=0,sT=0,mx=0; int n=0;
    for(int b=0;b<400;b++){ fill_stereo_noise(buf,128,&sl,&sr); api->process_block(inst,buf,128);
        for(int i=0;i<128;i++){ float L=buf[i*2]/32768.0f, R=buf[i*2+1]/32768.0f;
            if(!(L>=-1.5f&&L<=1.5f)||!(R>=-1.5f&&R<=1.5f)) n++;
            double d=L-R; sS+=d*d; sT+=(double)L*L+(double)R*R;
            double a=fabs(L)>fabs(R)?fabs(L):fabs(R); if(a>mx)mx=a; } }
    *maxa=mx; *nf=n; return sT>1e-9? sS/sT : 0.0;
}

int main(void){
    memset(&HOST,0,sizeof HOST);
    HOST.api_version=1; HOST.sample_rate=44100; HOST.frames_per_block=128;
    HOST.log=host_log; HOST.get_bpm=host_bpm; HOST.get_clock_status=host_clock;
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(&HOST);
    void *inst = api->create_instance("/tmp",NULL);
    CHECK("create_instance", inst!=NULL);
    if(!inst){ printf("FATAL\n"); return 1; }

    /* dry path, stereo input preserved, nothing else widening */
    api->set_param(inst,"mix","0"); api->set_param(inst,"space","0"); api->set_param(inst,"mod_depth","0");
    api->set_param(inst,"drift","0"); api->set_param(inst,"filter","1"); api->set_param(inst,"input_mode","Ster");
    api->set_param(inst,"tempo_src","Man");

    double mx0,mx1,mx2; int nf0,nf1,nf2;
    double mono = side_ratio(api, inst, "0.0", &mx0, &nf0);   /* w=0  -> mono */
    double neut = side_ratio(api, inst, "0.5", &mx1, &nf1);   /* w=1  -> neutral */
    double wide = side_ratio(api, inst, "1.0", &mx2, &nf2);   /* w=2  -> wide */
    printf("  Side ratio: mono=%.4f  neutral=%.4f  wide=%.4f\n", mono, neut, wide);
    printf("  peaks: %.3f / %.3f / %.3f\n", mx0, mx1, mx2);

    CHECK("Stereo=0 collapses to mono (Side ~ 0)", mono < 0.01);
    CHECK("neutral preserves the stereo image (Side present)", neut > 0.2);
    CHECK("Stereo up widens (Side boosted past neutral)", wide > neut * 1.3);
    CHECK("Stereo down narrows (mono < neutral)", mono < neut);
    CHECK("all settings finite + bounded", mx0<1.0 && mx1<1.0 && mx2<1.0 && nf0==0 && nf1==0 && nf2==0);

    /* param plumbing */
    api->set_param(inst,"preset","Init");
    char sb[32]; api->get_param(inst,"stereo",sb,sizeof sb);
    CHECK("stereo defaults to 0.5 (neutral)", fabs(atof(sb)-0.5)<1e-3);
    char big[8192]; api->get_param(inst,"chain_params",big,sizeof big);
    CHECK("chain_params exposes stereo (typed float)",
          strstr(big,"\"key\":\"stereo\",\"name\":\"Stereo\",\"type\":\"float\"")!=NULL);
    char hier[8192]; api->get_param(inst,"ui_hierarchy",hier,sizeof hier);
    char *sfx = strstr(hier,"\"spacefx\"");
    CHECK("stereo is on the Space FX page", sfx && strstr(sfx,"stereo")!=NULL);
    api->set_param(inst,"stereo","0.8");
    char state[2048]; api->get_param(inst,"state",state,sizeof state);
    CHECK("state JSON carries stereo", strstr(state,"\"stereo\":")!=NULL);
    api->set_param(inst,"stereo","0.2"); api->set_param(inst,"state",state);
    api->get_param(inst,"stereo",sb,sizeof sb);
    CHECK("state restore brings stereo back", fabs(atof(sb)-0.8)<1e-3);

    api->destroy_instance(inst);
    printf("\n%s (%d failures)\n", fails?"TESTS FAILED":"ALL TESTS PASSED", fails);
    return fails?1:0;
}
