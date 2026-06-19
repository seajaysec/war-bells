/* bench_mem.c — resident memory for N idle instances (no recording). Proves the looper
 * lazy-allocation: before, each instance pre-allocated ~21MB of looper buffers at create. */
#include <stdio.h>
#include <sys/resource.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);
int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    for (int k=0;k<4;k++){ void*i=api->create_instance(".",0);
        int16_t buf[256]={0}; for(int b=0;b<50;b++) api->process_block(i,buf,128); }  /* run, never record */
    struct rusage ru; getrusage(RUSAGE_SELF,&ru);
    double mb = ru.ru_maxrss / (1024.0*1024.0);   /* bytes on macOS */
    if (mb < 1.0) mb = ru.ru_maxrss / 1024.0;      /* KB on linux */
    printf("4 idle instances (no record): peak RSS = %.1f MB\n", mb);
    return 0;
}
