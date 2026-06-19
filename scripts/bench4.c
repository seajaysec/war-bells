/* bench4.c — realistic 4-instance load vs Activity. The worst-case bench pins Activity=1 (all
 * 6 voices live), which the idle-voice skip can't help. Real playing sits lower, leaving voices
 * idle — this shows that payoff. 4 instances, varied effects, half with shimmer. */
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);
static double now_us(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e6 + t.tv_nsec*1e-3; }
static uint32_t R=7u; static inline int16_t nz(void){ R^=R<<13;R^=R>>17;R^=R<<5; return (int16_t)((int)(R>>16)-16384)/8; }
int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    const double BUDGET=2902.0; const int N=4;
    const char *eff[4]={"Cloud","Seq","Drone","Warp"};
    void *inst[4]; int16_t bufs[4][256];
    for(int k=0;k<N;k++){ inst[k]=api->create_instance(".",0);
        api->set_param(inst[k],"effect",eff[k]); api->set_param(inst[k],"space","0.7");
        api->set_param(inst[k],"reverb_mode","Vast"); api->set_param(inst[k],"mix","0.6");
        api->set_param(inst[k],"shimmer", (k&1)?"Oct+":"Off"); }
    printf("budget 2902 us/block. 4 instances (Cloud/Seq/Drone/Warp, half shimmer) vs Activity:\n");
    const char *acts[]={"0.3","0.5","0.7","1.0"};
    for(int a=0;a<4;a++){
        for(int k=0;k<N;k++) api->set_param(inst[k],"activity",acts[a]);
        for(int b=0;b<600;b++) for(int k=0;k<N;k++){ for(int i=0;i<256;i++)bufs[k][i]=nz(); api->process_block(inst[k],bufs[k],128); }
        double sum=0,mx=0; const int M=3000;
        for(int b=0;b<M;b++){ double t0=now_us();
            for(int k=0;k<N;k++){ for(int i=0;i<256;i++)bufs[k][i]=nz(); api->process_block(inst[k],bufs[k],128); }
            double dt=now_us()-t0; sum+=dt; if(dt>mx)mx=dt; }
        printf("Activity %s  avg %7.1f us (%3.0f%%)   max %7.1f us (%3.0f%%)\n",
            acts[a], sum/M, 100*(sum/M)/BUDGET, mx, 100*mx/BUDGET);
    }
    return 0;
}
