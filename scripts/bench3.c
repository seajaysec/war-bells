/* bench3.c — cumulative load: N War Bells instances processed per block at worst case
 * (Cutup, shimmer on, activity 1), to find how many fit the shared audio budget. A live
 * rack runs several modules on one audio core, so this is the number that decides reliability. */
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);
static double now_us(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e6 + t.tv_nsec*1e-3; }
static uint32_t R=7u; static inline int16_t nz(void){ R^=R<<13;R^=R>>17;R^=R<<5; return (int16_t)((int)(R>>16)-16384)/4; }
int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    const double BUDGET=2902.0; const int MAXN=8;
    void *inst[MAXN]; int16_t bufs[MAXN][256];
    for(int n=0;n<MAXN;n++){ inst[n]=api->create_instance(".",0);
        api->set_param(inst[n],"effect","Cutup"); api->set_param(inst[n],"variation","2");
        api->set_param(inst[n],"activity","1"); api->set_param(inst[n],"repeats","0.95");
        api->set_param(inst[n],"space","0.9"); api->set_param(inst[n],"reverb_mode","Vast");
        api->set_param(inst[n],"mix","1"); api->set_param(inst[n],"shimmer","Oct+"); }
    printf("budget 2902 us/block. cumulative worst-case (each: Cutup+shimmer+activity1):\n");
    for(int N=1;N<=MAXN;N++){
        for(int b=0;b<400;b++) for(int k=0;k<N;k++){ for(int i=0;i<256;i++)bufs[k][i]=nz(); api->process_block(inst[k],bufs[k],128); }
        double sum=0,mx=0; const int M=3000;
        for(int b=0;b<M;b++){ double t0=now_us();
            for(int k=0;k<N;k++){ for(int i=0;i<256;i++)bufs[k][i]=nz(); api->process_block(inst[k],bufs[k],128); }
            double dt=now_us()-t0; sum+=dt; if(dt>mx)mx=dt; }
        printf("%dx  avg %7.1f us (%3.0f%%)   max %7.1f us (%3.0f%%)%s\n",
            N, sum/M, 100*(sum/M)/BUDGET, mx, 100*mx/BUDGET, (sum/M)>BUDGET?"  <== OVER":"");
    }
    return 0;
}
