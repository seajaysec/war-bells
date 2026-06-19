/* bench5.c — component cost breakdown (on the Move). Toggle each block via its param so I optimize
 * the part that actually dominates: reverb+space, grains, chorus, vs the fixed baseline. */
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);
static double now_us(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e6 + t.tv_nsec*1e-3; }
static uint32_t R=7u; static inline int16_t nz(void){ R^=R<<13;R^=R>>17;R^=R<<5; return (int16_t)((int)(R>>16)-16384)/8; }
static double meas(audio_fx_api_v2_t*api, void*inst){
    int16_t b[256]; for(int k=0;k<400;k++){for(int i=0;i<256;i++)b[i]=nz();api->process_block(inst,b,128);}
    double t0=now_us(); for(int k=0;k<5000;k++){for(int i=0;i<256;i++)b[i]=nz();api->process_block(inst,b,128);} return (now_us()-t0)/5000;
}
int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    void *inst = api->create_instance(".",0);
    #define S(k,v) api->set_param(inst,k,v)
    S("effect","Cloud"); S("mix","0.7"); S("reverb_mode","Vast");
    S("activity","0.7"); S("space","0.9"); S("mod_depth","0.6");
    double full=meas(api,inst);
    S("space","0.0"); double noRev=meas(api,inst); S("space","0.9");
    S("mod_depth","0.0"); double noCho=meas(api,inst); S("mod_depth","0.6");
    S("activity","0.02"); double noGr=meas(api,inst); S("activity","0.7");
    S("space","0.0"); S("mod_depth","0.0"); S("activity","0.02"); double base=meas(api,inst);
    printf("full              %6.1f us/block\n", full);
    printf("reverb+space cost %6.1f us  (full - space off)\n", full-noRev);
    printf("chorus cost       %6.1f us  (full - chorus off)\n", full-noCho);
    printf("grains cost(5v)   %6.1f us  (full - activity~0)\n", full-noGr);
    printf("baseline(io+dly+filt+1v) %6.1f us\n", base);
    return 0;
}
