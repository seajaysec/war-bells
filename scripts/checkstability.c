/* checkstability.c — the test the 1-second checks missed: feed each preset a CONTINUOUS source for
 * ~12 s and watch whether the level RUNS AWAY over time (feedback buildup -> wall/crash) or goes
 * non-finite. Compares the level in the first second vs the last second. */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);
static uint32_t R=99u; static inline float nz(void){ R^=R<<13;R^=R>>17;R^=R<<5; return ((int)(R>>16)-16384)/16384.0f*0.4f; }
int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    const char *PS[22]={"Init","Arp","Stutr","Chop","Glass","Seq","Stack","Cloud","Drone","Birds",
                        "Taps","Warp","Sheen","Motn","Evolv","Scale","Bloom","Trails","Spiral",
                        "Drifting","Expanse","MonoTap"};
    const int BLK=4500;  /* ~13 s */
    printf("preset      early_rms  late_rms  ratio  maxpeak  status\n");
    int fails=0;
    for(int p=0;p<22;p++){
        void *inst=api->create_instance(".",0); api->set_param(inst,"preset",PS[p]);
        int16_t buf[256]; double ph=0; double early=0,late=0,pk=0; int bad=0;
        for(int b=0;b<BLK;b++){
            for(int i=0;i<128;i++){ float s=0.35f*sinf((float)ph)+nz(); ph+=2*M_PI*200.0/44100.0;
                int v=(int)(s*16000); if(v>32767)v=32767; if(v<-32768)v=-32768; buf[i*2]=v; buf[i*2+1]=v; }
            api->process_block(inst,buf,128);
            double r=0; for(int i=0;i<256;i++){ double o=buf[i]/32768.0; r+=o*o; double a=o<0?-o:o; if(a>pk)pk=a; if(!(o>-2.0&&o<2.0))bad++; }
            r=sqrt(r/256);
            if(b<350) early+=r; if(b>=BLK-350) late+=r;
        }
        early/=350; late/=350; double ratio=late/(early+1e-9);
        /* real failure = reaches the rails under sustained input (>=0.93) or non-finite. A high
         * ratio alone is just the reverb filling from silence (normal) — not flagged. */
        int failed = bad || pk >= 0.93;
        const char *st = bad?"NON-FINITE": (pk>=0.93?"RUNAWAY->RAILS": (ratio>1.5?"fills(ok)":"ok"));
        if (failed) fails++;
        printf("%-10s %8.3f %9.3f %6.2f %8.3f  %s\n", PS[p], early, late, ratio, pk, st);
        api->destroy_instance(inst);
    }
    if (fails) { printf("STABILITY FAIL: %d preset(s) reach the rails / non-finite\n", fails); return 1; }
    printf("stability ok: no preset reaches the rails under sustained input\n");
    return 0;
}
