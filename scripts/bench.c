/* bench.c — real-time CPU profiler for War Bells, run ON the Move (aarch64).
 * Times process_block() for the worst case of every effect (x shimmer, x variation)
 * against the per-block budget (128 frames @ 44.1k = 2902 us). In a live rack several
 * modules SHARE the CPU, so War Bells alone should sit well under budget. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

static double now_us(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e6 + t.tv_nsec*1e-3; }
static uint32_t R=2463534242u;
static inline int16_t nz(void){ R^=R<<13; R^=R>>17; R^=R<<5; return (int16_t)((int)(R>>16)-16384)/4; } /* ~±0.12 */

int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    void *inst = api->create_instance(".", 0);
    int16_t buf[128*2];
    const int BLOCKS=6000, WARM=400;
    const double BUDGET=128.0/44100.0*1e6;   /* 2902 us */
    const char *effs[]={"Arp","Cutup","Chop","Glide","Seq","Stack","Cloud","Drone","Chain","Taps","Warp"};

    /* heavy global settings shared by all */
    api->set_param(inst,"activity","1");  api->set_param(inst,"repeats","0.95");
    api->set_param(inst,"shape","0.9");   api->set_param(inst,"mix","1");
    api->set_param(inst,"effect_vol","1");api->set_param(inst,"space","0.9");
    api->set_param(inst,"reverb_mode","Vast"); api->set_param(inst,"width","1");
    api->set_param(inst,"filter","0.5");  api->set_param(inst,"filter_res","0.85");
    api->set_param(inst,"mod_depth","0.8");api->set_param(inst,"mod_rate","0.6");

    printf("budget = %.0f us/block (128 @ 44.1k). Several modules share the CPU.\n", BUDGET);
    printf("%-7s %-4s %9s %8s   %s\n","effect","shim","us/block","%budget","");
    double gmax=0; char gwho[64]="";
    for(int shim=0; shim<2; shim++){
        api->set_param(inst,"shimmer", shim?"Oct+":"Off");
        for(int e=0;e<11;e++){
            api->set_param(inst,"effect",effs[e]);
            double worst=0; int wv=0;
            for(int v=0;v<4;v++){
                char vb[4]; snprintf(vb,sizeof vb,"%d",v); api->set_param(inst,"variation",vb);
                for(int b=0;b<WARM;b++){ for(int i=0;i<256;i++)buf[i]=nz(); api->process_block(inst,buf,128); }
                double t0=now_us();
                for(int b=0;b<BLOCKS;b++){ for(int i=0;i<256;i++)buf[i]=nz(); api->process_block(inst,buf,128); }
                double us=(now_us()-t0)/BLOCKS;
                if(us>worst){worst=us;wv=v;}
            }
            double pc=100*worst/BUDGET;
            printf("%-7s %-4s %9.1f %7.0f%%   var%d%s\n", effs[e], shim?"on":"off", worst, pc, wv,
                   pc>50?"  <== HEAVY": pc>33?"  < watch":"");
            if(worst>gmax){gmax=worst; snprintf(gwho,sizeof gwho,"%s shim=%s var%d",effs[e],shim?"on":"off",wv);}
        }
    }
    printf("\nWORST: %.1f us/block (%.0f%% of budget) — %s\n", gmax, 100*gmax/BUDGET, gwho);
    return 0;
}
