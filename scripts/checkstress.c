/* checkstress.c — reusable stress + artifact audit driver.
 *
 * Runs a matrix of CASES (presets ∪ single-param sweeps ∪ worst-case combo ∪ seeded Monte-Carlo ∪
 * spectral tone probes) through the real engine and scores EACH artifact metric (see wb_metrics.h) per
 * case. Prints a per-metric scorecard, writes tests/metrics_report.json, and (if tests/metrics_baseline.json
 * exists) fails on any per-metric REGRESSION — so each metric's success is tracked over time, not collapsed
 * to one green/red. Also fails on any absolute artifact (a case breaching a metric threshold).
 *
 * Input per case: ~4 s of a representative source (tone + noise + periodic transients) to drive feedback to
 * steady state, then ~1 s of silence (denormal-stall timing on decay). Spectral cases feed a pure 220 Hz
 * tone and analyze the steady-state spectrum.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"
#include "wb_metrics.h"
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

/* ---- metric ids + thresholds (artifact boundaries; higher = worse unless noted) ---- */
enum { M_SILENCE, M_RUNAWAY, M_TRUEPEAK, M_CLICKS, M_DC, M_NONFIN, M_DENORM, M_HARSH, M_ALIAS, M_N };
static const char *MNAME[M_N] = { "silence","runaway","true_peak","discontinuity","dc_offset","non_finite","denormal","harshness","aliasing" };
static const double MTHR[M_N] = { 0.004,     0.92,     0.999,      90.0,           0.02,       0.5,         6.0,       0.25,      0.20 };
static const int    MHW [M_N] = { 0,         1,        1,          1,              1,          1,           1,         1,         1   };  /* higher_worse */

typedef struct { double worst; int fails; char worst_case[48]; int seen; } agg_t;

/* ---- source generator ---- */
enum { SRC_MIX, SRC_TONE, SRC_SILENCE };
static uint32_t RNG = 2463534242u;
static float frand(void){ RNG^=RNG<<13; RNG^=RNG>>17; RNG^=RNG<<5; return (float)(RNG>>8)*(1.0f/16777216.0f); }
static void gen(int16_t *buf, int n, int mode, double *ph, double f0, long *t){
    for(int i=0;i<n;i++){
        float s=0;
        if(mode==SRC_MIX){
            s = 0.30f*sinf((float)*ph); *ph += 2.0*M_PI*180.0/44100.0;
            s += 0.22f*(frand()*2.0f-1.0f);
            if((*t % 13230) < 300){ float e=1.0f-(float)(*t%13230)/300.0f; s += 0.5f*e*sinf((float)(*t)*0.18f); } /* transient bursts ~3.3/s */
        } else if(mode==SRC_TONE){ s = 0.40f*sinf((float)*ph); *ph += 2.0*M_PI*f0/44100.0; }
        int v=(int)(s*16000.0f); if(v>32767)v=32767; if(v<-32768)v=-32768;
        buf[i*2]=v; buf[i*2+1]=v; (*t)++;
    }
}

/* ---- one case ---- */
typedef struct { char name[48]; char key[24][24]; char val[24][24]; int nset; int spectral; int transition; } caseT;
static const char *PSALL[19]={"Init","Arp","Stutr","Chop","Glass","Seq","Stack","Cloud","Drone","Birds",
                              "Taps","Warp","Sheen","Motn","Evolv","Scale","Bloom","Trails","Spiral"};

static double now_us(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1e6 + ts.tv_nsec*1e-3; }

static void run_case(audio_fx_api_v2_t *api, const caseT *c, double out[M_N]){
    void *inst = api->create_instance(".",0);
    for(int i=0;i<c->nset;i++) api->set_param(inst, c->key[i], c->val[i]);
    for(int k=0;k<M_N;k++) out[k]=0;
    int16_t buf[256]; double ph=0; long t=0;

    if(c->spectral){
        for(int b=0;b<700;b++){ gen(buf,128,SRC_TONE,&ph,220.0,&t); api->process_block(inst,buf,128); } /* settle ~2s */
        static float cap[16384]; int cp=0;
        while(cp<16384){ gen(buf,128,SRC_TONE,&ph,220.0,&t); api->process_block(inst,buf,128);
            for(int i=0;i<128 && cp<16384;i++) cap[cp++]=0.5f*(buf[i*2]+buf[i*2+1])/32768.0f; }
        double thd=0,hf=0; wb_spectral(cap,16384,220.0,44100.0,&thd,&hf);
        out[M_HARSH]=hf; out[M_ALIAS]=thd;
        api->destroy_instance(inst); return;
    }

    /* sustained mixed input ~4s */
    const int SUST=1378; /* blocks (~4s) */
    wb_acc acc; wb_acc_init(&acc, (long)SUST*128);
    double act_us=0; long act_blk=0;
    for(int b=0;b<SUST;b++){ gen(buf,128,SRC_MIX,&ph,0,&t);
        if(c->transition && b>0 && b%172==0) api->set_param(inst,"preset",PSALL[(b/172)%19]); /* switch every ~0.5s */
        double t0=now_us(); api->process_block(inst,buf,128); act_us += now_us()-t0; act_blk++;
        float L[128],R[128]; for(int i=0;i<128;i++){ L[i]=buf[i*2]/32768.0f; R[i]=buf[i*2+1]/32768.0f; }
        wb_acc_feed(&acc,L,R,128);
    }
    double act_mean = act_blk? act_us/act_blk : 1.0;
    /* silence tail ~1s, chunked timing for denormal-stall. Use the MEDIAN chunk rate, not the
     * max: a real denormal stall slows every decay block (median rises), while a single slow
     * chunk is just OS scheduler jitter (median rejects it). Taking the max made this metric a
     * machine-load gauge that flagged false regressions under CI load — median measures the
     * stall itself, so the absolute threshold and the baseline both hold across noisy runs. */
    double chrate[6]; int CH=64;
    for(int chunk=0; chunk<6; chunk++){
        double cu=0; for(int b=0;b<CH;b++){ memset(buf,0,sizeof(buf));
            double t0=now_us(); api->process_block(inst,buf,128); cu += now_us()-t0;
            for(int i=0;i<256;i++){ double o=buf[i]/32768.0; if(!(o>-8.0&&o<8.0)) acc.nonfinite++; } }
        chrate[chunk]=cu/CH;
    }
    for(int a=1;a<6;a++){ double v=chrate[a]; int b=a-1; while(b>=0&&chrate[b]>v){chrate[b+1]=chrate[b];b--;} chrate[b+1]=v; }
    double med_chunk_rate = 0.5*(chrate[2]+chrate[3]);   /* median of 6 */
    out[M_SILENCE]  = wb_rms(&acc);
    out[M_RUNAWAY]  = acc.late_peak;
    out[M_TRUEPEAK] = acc.truepeak;
    out[M_CLICKS]   = wb_click_rate(&acc);
    out[M_DC]       = wb_dc(&acc);
    out[M_NONFIN]   = (double)acc.nonfinite;
    out[M_DENORM]   = act_mean>1e-6 ? med_chunk_rate/act_mean : 0.0;
    out[M_HARSH]    = 0; out[M_ALIAS]=0;  /* spectral only */
    api->destroy_instance(inst);
}

/* ---- case matrix builder ---- */
static caseT CASES[256]; static int NC=0;
static caseT *newcase(const char *name){ caseT *c=&CASES[NC++]; memset(c,0,sizeof(*c)); snprintf(c->name,sizeof(c->name),"%s",name); return c; }
static void add(caseT *c, const char *k, const char *v){ snprintf(c->key[c->nset],24,"%s",k); snprintf(c->val[c->nset],24,"%s",v); c->nset++; }

int main(void){
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(0);
    const char *PS[19]={"Init","Arp","Stutr","Chop","Glass","Seq","Stack","Cloud","Drone","Birds",
                        "Taps","Warp","Sheen","Motn","Evolv","Scale","Bloom","Trails","Spiral"};
    /* presets */
    for(int p=0;p<19;p++){ caseT *c=newcase(PS[p]); add(c,"preset",PS[p]); }
    /* single-param sweeps over high-risk floats (from a wet Stack base) */
    const char *SW[]={"sustain","space","activity","repeats","mod_depth","mot_depth","warp","effect_vol","mix","duck","rev_tone","drift"};
    for(unsigned s=0;s<sizeof(SW)/sizeof(*SW);s++){
        double steps[5]={0.0,0.25,0.5,0.75,1.0};
        for(int i=0;i<5;i++){ char nm[48],vv[16]; snprintf(nm,sizeof(nm),"sweep:%s=%.2f",SW[s],steps[i]); snprintf(vv,16,"%.3f",steps[i]);
            caseT *c=newcase(nm); add(c,"preset","Stack"); add(c,"space","0.5"); add(c,SW[s],vv); }
    }
    /* enum sweeps */
    for(int rm=0;rm<4;rm++){ char nm[48],vv[8]; snprintf(nm,sizeof(nm),"sweep:reverb=%d",rm); snprintf(vv,8,"%d",rm);
        caseT *c=newcase(nm); add(c,"preset","Stack"); add(c,"space","0.7"); add(c,"reverb_mode",vv); }
    for(int sh=0;sh<4;sh++){ char nm[48],vv[8]; snprintf(nm,sizeof(nm),"sweep:shimmer=%d",sh); snprintf(vv,8,"%d",sh);
        caseT *c=newcase(nm); add(c,"preset","Stack"); add(c,"space","0.7"); add(c,"shimmer",vv); }
    /* worst-case combo: everything that feeds the loops, maxed */
    { caseT *c=newcase("WORST-COMBO"); add(c,"effect","5"); add(c,"activity","1"); add(c,"repeats","1");
      add(c,"mix","1"); add(c,"effect_vol","1"); add(c,"space","1"); add(c,"reverb_mode","3"); add(c,"shimmer","1");
      add(c,"sustain","1"); add(c,"warp","0.9"); add(c,"mod_depth","1"); add(c,"mot_target","6"); add(c,"mot_depth","1");
      add(c,"filter_res","0.9"); }
    /* mode-transition stress: switch presets every ~0.5s mid-stream (state must settle, not click/runaway) */
    { caseT *c=newcase("TRANSITIONS"); c->transition=1; add(c,"preset","Cloud"); }
    /* seeded Monte-Carlo over high-risk params */
    const char *MC[]={"activity","repeats","shape","mix","effect_vol","space","filter","filter_res","sustain","warp","mod_depth","mot_depth","duck","evolve","width"};
    for(int r=0;r<14;r++){ char nm[48]; snprintf(nm,sizeof(nm),"MC#%02d",r); caseT *c=newcase(nm);
        add(c,"effect","5"); add(c,"reverb_mode", (frand()<0.5f)?"2":"3"); add(c,"shimmer",(frand()<0.5f)?"1":"0");
        for(unsigned m=0;m<sizeof(MC)/sizeof(*MC);m++){ char vv[16]; snprintf(vv,16,"%.3f",frand()); add(c,MC[m],vv); } }
    /* spectral tone probes (aliasing/harshness) */
    { caseT *c=newcase("spectral:clean");  c->spectral=1; add(c,"preset","Init"); }
    { caseT *c=newcase("spectral:warp");   c->spectral=1; add(c,"preset","Stack"); add(c,"warp","0.95"); add(c,"space","0.6"); }
    { caseT *c=newcase("spectral:shimmer");c->spectral=1; add(c,"preset","Sheen"); }
    { caseT *c=newcase("spectral:worst");  c->spectral=1; add(c,"preset","Stack"); add(c,"shimmer","1"); add(c,"sustain","1"); add(c,"space","1"); add(c,"reverb_mode","3"); }

    /* run all, aggregate per metric */
    agg_t ag[M_N]; for(int k=0;k<M_N;k++){ ag[k].worst = MHW[k]?-1e9:1e9; ag[k].fails=0; ag[k].seen=0; ag[k].worst_case[0]=0; }
    int failcases=0;
    for(int i=0;i<NC;i++){
        double out[M_N]; run_case(api,&CASES[i],out);
        int casebad=0;
        for(int k=0;k<M_N;k++){
            if(CASES[i].spectral != (k==M_HARSH||k==M_ALIAS)) continue;   /* spectral metrics only on spectral cases, and vice-versa */
            ag[k].seen=1;
            wb_metric mm; wb_metric_set(&mm, MNAME[k], out[k], MTHR[k], MHW[k]);
            int worse = MHW[k] ? (out[k] > ag[k].worst) : (out[k] < ag[k].worst);
            if(worse){ ag[k].worst=out[k]; snprintf(ag[k].worst_case,48,"%s",CASES[i].name); }
            if(!mm.pass){ ag[k].fails++; casebad=1; }
        }
        if(casebad) failcases++;
    }

    /* scorecard */
    printf("\n=== WAR BELLS STRESS SCORECARD (%d cases) ===\n", NC);
    printf("%-14s %10s %10s %6s  %s\n","metric","worst","threshold","fails","worst_case");
    int total_fail=0;
    for(int k=0;k<M_N;k++){ if(!ag[k].seen) continue;
        printf("%-14s %10.4f %10.4f %6d  %s%s\n", MNAME[k], ag[k].worst, MTHR[k], ag[k].fails,
               ag[k].worst_case, ag[k].fails?"   <-- FAIL":"");
        total_fail += ag[k].fails;
    }
    printf("cases with >=1 artifact: %d / %d\n", failcases, NC);

    /* write report */
    FILE *rep=fopen("tests/metrics_report.json","w");
    if(rep){ fprintf(rep,"{\n  \"metrics\": {\n");
        int first=1;
        for(int k=0;k<M_N;k++){ if(!ag[k].seen) continue;
            fprintf(rep,"%s    \"%s\": {\"worst\": %.5f, \"threshold\": %.5f, \"higher_worse\": %d, \"fails\": %d, \"worst_case\": \"%s\"}",
                    first?"":",\n", MNAME[k], ag[k].worst, MTHR[k], MHW[k], ag[k].fails, ag[k].worst_case); first=0; }
        fprintf(rep,"\n  }\n}\n"); fclose(rep);
    }

    /* baseline regression check */
    int regressions=0;
    FILE *bl=fopen("tests/metrics_baseline.json","r");
    if(bl){ char line[512];
        while(fgets(line,sizeof(line),bl)){
            for(int k=0;k<M_N;k++){ char pat[64]; snprintf(pat,sizeof(pat),"\"%s\":",MNAME[k]);
                char *p=strstr(line,pat); if(!p) continue;
                char *w=strstr(p,"\"worst\":"); if(!w) continue; double bw=atof(w+8);
                double cur=ag[k].worst; int worse = MHW[k] ? (cur > bw*1.05 + 1e-4) : (cur < bw*0.95 - 1e-4);
                if(ag[k].seen && worse){ printf("REGRESSION %s: %.4f worse than baseline %.4f\n",MNAME[k],cur,bw); regressions++; }
            }
        }
        fclose(bl);
    } else {
        printf("(no tests/metrics_baseline.json — run is informational; commit a baseline once green)\n");
    }

    if(total_fail>0){ printf("\nSTRESS FAIL: %d metric breaches across the matrix\n", total_fail); return 1; }
    if(regressions>0){ printf("\nSTRESS FAIL: %d metric regression(s) vs baseline\n", regressions); return 1; }
    printf("\nstress ok: no artifact thresholds breached, no regressions\n");
    return 0;
}
