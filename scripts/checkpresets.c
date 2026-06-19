/* checkpresets.c — does each preset actually PRODUCE SOUND? (the test "bounded" missed: silence
 * passes a bounds check). Feed a steady tone+noise through every preset and print output level. */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);
static uint32_t R=22u; static inline float nz(void){ R^=R<<13;R^=R>>17;R^=R<<5; return ((int)(R>>16)-16384)/16384.0f; }
int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    const char *PS[19]={"Init","Arp","Stutr","Chop","Glass","Seq","Stack","Cloud","Drone","Birds",
                        "Taps","Warp","Sheen","Motn","Evolv","Scale","Bloom","Trails","Spiral"};
    printf("preset       out_rms   peak  (input rms ~ 4600)\n");
    for(int p=0;p<19;p++){
        void *inst=api->create_instance(".",0);
        api->set_param(inst,"preset",PS[p]);
        int16_t buf[256]; double ph=0; double sq=0; int pk=0; long n=0;
        for(int b=0;b<400;b++){
            for(int i=0;i<128;i++){ float s=0.5f*sinf((float)ph)+0.15f*nz(); ph+=2*M_PI*220.0/44100.0;
                int16_t v=(int16_t)(s*16000); buf[i*2]=v; buf[i*2+1]=v; }
            api->process_block(inst,buf,128);
            if(b>=200){ for(int i=0;i<256;i++){ int a=buf[i]<0?-buf[i]:buf[i]; if(a>pk)pk=a; sq+=(double)buf[i]*buf[i]; n++; } }
        }
        double rms=sqrt(sq/n); double pkf=pk/32768.0;
        const char *flag = pkf>0.985?"  <== CLIPPING" : (rms<150.0?"  <== SILENT" : "");
        printf("%-10s rms=%7.1f peak=%.3f%s\n", PS[p], rms, pkf, flag);
        api->destroy_instance(inst);
    }
    return 0;
}
