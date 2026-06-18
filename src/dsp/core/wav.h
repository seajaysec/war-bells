/* wav.h — minimal stereo int16 WAV read/write (44.1k) for looper persistence.
 * Called only from set_param (non-realtime); never from process_block. */
#ifndef WB_WAV_H
#define WB_WAV_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static inline void wb__w32(FILE *f, uint32_t v){ fputc(v&0xff,f);fputc((v>>8)&0xff,f);fputc((v>>16)&0xff,f);fputc((v>>24)&0xff,f); }
static inline void wb__w16(FILE *f, uint16_t v){ fputc(v&0xff,f);fputc((v>>8)&0xff,f); }
static inline uint32_t wb__r32(FILE *f){ int a=fgetc(f),b=fgetc(f),c=fgetc(f),d=fgetc(f); return (uint32_t)(a|(b<<8)|(c<<16)|(d<<24)); }
static inline uint16_t wb__r16(FILE *f){ int a=fgetc(f),b=fgetc(f); return (uint16_t)(a|(b<<8)); }

/* write `frames` of stereo int16 (separate L/R) as a WAV. returns 0 ok, -1 err. */
static inline int wb_wav_write(const char *path, const int16_t *l, const int16_t *r, int frames) {
    if (frames < 1) return -1;
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    uint32_t data_bytes = (uint32_t)frames * 2u * 2u;  /* 2 ch * 2 bytes */
    fwrite("RIFF",1,4,f); wb__w32(f, 36 + data_bytes); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f); wb__w32(f,16); wb__w16(f,1); wb__w16(f,2);
    wb__w32(f,44100); wb__w32(f,44100*2*2); wb__w16(f,4); wb__w16(f,16);
    fwrite("data",1,4,f); wb__w32(f, data_bytes);
    for (int i=0;i<frames;i++){ wb__w16(f,(uint16_t)l[i]); wb__w16(f,(uint16_t)r[i]); }
    fclose(f);
    return 0;
}

/* read a stereo int16 WAV into l/r (up to cap frames). returns frames read, or -1. */
static inline int wb_wav_read(const char *path, int16_t *l, int16_t *r, int cap) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    char tag[4];
    if (fread(tag,1,4,f)!=4 || memcmp(tag,"RIFF",4)) { fclose(f); return -1; }
    wb__r32(f); fread(tag,1,4,f); /* WAVE */
    int ch=2; uint32_t data_bytes=0; long data_pos=-1;
    while (fread(tag,1,4,f)==4) {
        uint32_t sz = wb__r32(f);
        if (!memcmp(tag,"fmt ",4)) {
            wb__r16(f); ch=wb__r16(f); wb__r32(f); wb__r32(f); wb__r16(f); wb__r16(f);
            if (sz>16) fseek(f,(long)(sz-16),SEEK_CUR);
        } else if (!memcmp(tag,"data",4)) {
            data_bytes=sz; data_pos=ftell(f); break;
        } else { fseek(f,(long)sz,SEEK_CUR); }
    }
    if (data_pos<0 || ch<1) { fclose(f); return -1; }
    int frames = (int)(data_bytes / (uint32_t)(ch*2));
    if (frames>cap) frames=cap;
    for (int i=0;i<frames;i++){
        int16_t a=(int16_t)wb__r16(f);
        int16_t b=(ch>1)?(int16_t)wb__r16(f):a;
        l[i]=a; r[i]=b;
    }
    fclose(f);
    return frames;
}

#endif /* WB_WAV_H */
