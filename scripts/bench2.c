/* bench2.c — the case the averaging bench misses: CPU during the SILENT tail
 * (where feedback decays into denormals on FPUs without flush-to-zero) and the
 * worst SINGLE block (a spike over budget = one dropout = a live glitch/crash).
 * Excite shimmer+reverb with noise, then go silent and watch the tail. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);
static double now_us(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e6 + t.tv_nsec*1e-3; }
static uint32_t R=99u; static inline int16_t nz(void){ R^=R<<13;R^=R>>17;R^=R<<5; return (int16_t)((int)(R>>16)-16384)/4; }

static void run(audio_fx_api_v2_t*api, void*inst, const char*name){
    int16_t buf[256]; const double BUDGET=2902.0;
    /* excite the shimmer/reverb tails */
    for(int b=0;b<3000;b++){ for(int i=0;i<256;i++)buf[i]=nz(); api->process_block(inst,buf,128); }
    /* now SILENCE — the tail decays toward denormal territory */
    double sum=0,mx=0; const int N=12000;
    for(int b=0;b<N;b++){ memset(buf,0,sizeof buf);
        double t0=now_us(); api->process_block(inst,buf,128); double dt=now_us()-t0;
        sum+=dt; if(dt>mx)mx=dt; }
    printf("%-10s silence: avg %.1f us/blk (%.0f%%)   MAX %.1f us/blk (%.0f%%)%s\n",
        name, sum/N, 100*(sum/N)/BUDGET, mx, 100*mx/BUDGET, mx>BUDGET?"  <== OVER BUDGET (dropout)":"");
}
int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    void *inst = api->create_instance(".", 0);
    api->set_param(inst,"effect","Cloud"); api->set_param(inst,"activity","0.6");
    api->set_param(inst,"space","0.9"); api->set_param(inst,"reverb_mode","Vast");
    api->set_param(inst,"mix","1"); api->set_param(inst,"repeats","0.9");
    printf("budget 2902 us/block. silent-tail test (denormal-sensitive):\n");
    api->set_param(inst,"shimmer","Off"); run(api,inst,"shimmer off");
    api->set_param(inst,"shimmer","Oct+"); run(api,inst,"shimmer on");
    return 0;
}
