/* bench_ab.c — fidelity guard. Renders identical seeded noise through configs that exercise
 * every path the CPU work touches, printing peak + RMS per config. Build for baseline and new,
 * diff the output: any audible change shows as a peak/RMS drift. (Runs natively; numerics match.) */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

typedef struct { const char *k, *v; } KV;
static void cfg(audio_fx_api_v2_t*api, void*inst, KV*kv, int n){ for(int i=0;i<n;i++) api->set_param(inst,kv[i].k,kv[i].v); }

static void measure(audio_fx_api_v2_t*api, const char*name, KV*kv, int n){
    void *inst = api->create_instance(".",0);
    /* a fixed baseline so every config starts identical */
    KV base[]={{"mix","0.5"},{"effect_vol","0.6"},{"shape","0.5"}};
    cfg(api,inst,base,3); cfg(api,inst,kv,n);
    uint32_t R=2246822519u; int16_t buf[256];
    double sq=0; int pk=0; long ns=0;
    for(int b=0;b<5000;b++){
        for(int i=0;i<256;i++){ R^=R<<13;R^=R>>17;R^=R<<5; buf[i]=(int16_t)(((int)(R>>16)-16384)/28); }
        api->process_block(inst,buf,128);
        if(b>=600){ for(int i=0;i<256;i++){ int a=buf[i]<0?-buf[i]:buf[i]; if(a>pk)pk=a; sq+=(double)buf[i]*buf[i]; ns++; } }
    }
    api->destroy_instance(inst);
    printf("%-22s peak=%6d  rms=%10.3f\n", name, pk, sqrt(sq/ns));
}
int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    KV c1[]={{"effect","Cloud"},{"variation","2"},{"activity","1"},{"space","0.9"},{"reverb_mode","Vast"}};
    KV c2[]={{"effect","Drone"},{"variation","1"},{"activity","1"},{"filter","0.4"},{"filter_res","0.85"}};
    KV c3[]={{"effect","Seq"},{"variation","2"},{"activity","0.8"},{"filter","0.5"},{"filter_res","0.7"}};
    KV c4[]={{"effect","Warp"},{"variation","0"},{"activity","0.7"},{"space","0.6"}};
    KV c5[]={{"effect","Chop"},{"activity","0.7"},{"mod_depth","0.8"},{"mod_rate","0.6"}};
    KV c6[]={{"effect","Cloud"},{"activity","0.8"},{"mot_target","Filter"},{"mot_shape","Sine"},{"mot_depth","0.9"},{"mot_rate","0.5"}};
    KV c7[]={{"effect","Stack"},{"activity","0.9"},{"space","0.9"},{"reverb_mode","Vast"},{"shimmer","Oct+"}};
    KV c8[]={{"effect","Cloud"},{"variation","2"},{"activity","0.2"},{"space","0.7"}}; /* idle-voice path */
    measure(api,"Cloud dense",     c1, sizeof c1/sizeof*c1);
    measure(api,"Drone bandpass",  c2, sizeof c2/sizeof*c2);
    measure(api,"Seq filter+res",  c3, sizeof c3/sizeof*c3);
    measure(api,"Warp taps",       c4, sizeof c4/sizeof*c4);
    measure(api,"Chop chorus",     c5, sizeof c5/sizeof*c5);
    measure(api,"Motion->Filter",  c6, sizeof c6/sizeof*c6);
    measure(api,"Stack shimmer",   c7, sizeof c7/sizeof*c7);
    measure(api,"Low activity 0.2",c8, sizeof c8/sizeof*c8);
    return 0;
}
