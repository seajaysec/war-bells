/* checklimit.c — verify the tape ceiling gets WARMER (darker), not brighter, when pushed.
 * Feeds a tone+harmonic at a clean level vs a hot level through a dry-ish chain and compares
 * spectral brightness (first-difference energy / total) + peak. Pushed should darken + not clip. */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);
static void meas(audio_fx_api_v2_t*api, void*inst, float amp, const char*tag){
    int16_t buf[256]; double ph=0, hf=0, tot=0, prev=0; int pk=0;
    for(int b=0;b<200;b++){
        for(int i=0;i<128;i++){ float s=(sinf((float)ph)+0.6f*sinf((float)ph*5.3f))*amp;
            int v=(int)s; if(v>32767)v=32767; if(v<-32768)v=-32768; buf[i*2]=v; buf[i*2+1]=v; ph+=2*M_PI*180.0/44100.0; }
        api->process_block(inst,buf,128);
        if(b>40) for(int i=0;i<128;i++){ double o=buf[i*2]/32768.0, d=o-prev; hf+=d*d; tot+=o*o; prev=o; int a=buf[i*2]<0?-buf[i*2]:buf[i*2]; if(a>pk)pk=a; }
    }
    printf("  %-7s brightness=%.4f  peak=%.3f\n", tag, hf/(tot+1e-9), pk/32768.0);
}
int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    void *inst=api->create_instance(".",0);
    #define S(k,v) api->set_param(inst,k,v)
    S("preset","Cloud");   /* a genuinely hot/wet cinematic preset */
    meas(api,inst,1500.0f,"clean");    /* quiet input — limiter idle */
    meas(api,inst,30000.0f,"pushed");  /* loud input — limiter working hard */
    printf("(pushed brightness should be <= clean; tape ceiling darkens, never brightens)\n");
    return 0;
}
