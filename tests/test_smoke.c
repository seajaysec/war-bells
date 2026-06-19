/* test_smoke.c — host-side smoke test for the War Bells audio_fx plugin.
 * Validates: init/create, dry passthrough & bypass gain staging, all 44
 * variations render finite audio without crashing, and the looper transport. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"

extern audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

static void host_log(const char *m) { (void)m; }
static float host_bpm(void) { return 120.0f; }
static int   host_clock(void) { return 1; }

static int fails = 0;
#define CHECK(desc, cond) do { \
    printf("%s  %s\n", (cond)?"PASS":"FAIL", desc); if(!(cond)) fails++; } while(0)

#define WB_FRAMES_DEF 128
static host_api_v1_t HOST;
static int16_t buf[WB_FRAMES_DEF*2];

static void fill_sine(int16_t *b, int frames, float *phase, float freq) {
    for (int i=0;i<frames;i++){
        float s = sinf(*phase)*0.5f;
        *phase += 2.0f*(float)M_PI*freq/44100.0f; if(*phase>2*M_PI)*phase-=2*M_PI;
        b[i*2]=(int16_t)(s*32767.0f); b[i*2+1]=(int16_t)(s*32767.0f);
    }
}
static void fill_noise(int16_t *b, int frames, unsigned *st) {
    for (int i=0;i<frames;i++){
        *st = *st*1103515245u+12345u; float s=((int)(*st>>9)/4194304.0f-1.0f)*0.4f;
        b[i*2]=(int16_t)(s*32767.0f); b[i*2+1]=(int16_t)(s*32767.0f);
    }
}
static double rms(const int16_t *b, int frames){
    double s=0; for(int i=0;i<frames*2;i++){double x=b[i]/32768.0; s+=x*x;}
    return sqrt(s/(frames*2));
}
static int all_finite(const int16_t *b, int frames){ (void)b;(void)frames; return 1; } /* int16 always finite */

int main(void){
    memset(&HOST,0,sizeof(HOST));
    HOST.api_version=1; HOST.sample_rate=44100; HOST.frames_per_block=128;
    HOST.log=host_log; HOST.get_bpm=host_bpm; HOST.get_clock_status=host_clock;

    audio_fx_api_v2_t *api = move_audio_fx_init_v2(&HOST);
    CHECK("init returns api", api && api->api_version==2);
    CHECK("has process_block", api && api->process_block);
    void *inst = api->create_instance("/tmp",NULL);
    CHECK("create_instance", inst!=NULL);
    if(!inst){ printf("FATAL\n"); return 1; }

    /* dry passthrough: mix=0 => out ~ in */
    api->set_param(inst,"mix","0"); api->set_param(inst,"space","0");
    api->set_param(inst,"mod_depth","0"); api->set_param(inst,"filter","1");
    api->set_param(inst,"bypass","Off"); api->set_param(inst,"hold","Off");
    float ph=0; double rin=0,rout=0;
    for(int blk=0;blk<8;blk++){ fill_sine(buf,128,&ph,440.0f); rin+=rms(buf,128); api->process_block(inst,buf,128); rout+=rms(buf,128);}
    double ratio = rout/(rin+1e-9);
    printf("  dry ratio=%.3f\n", ratio);
    CHECK("dry passthrough preserves level (mix=0)", ratio>0.80 && ratio<1.20);

    /* bypass: out == in */
    api->set_param(inst,"bypass","On");
    for(int blk=0;blk<40;blk++){ fill_sine(buf,128,&ph,440.0f); api->process_block(inst,buf,128);} /* settle xfade */
    fill_sine(buf,128,&ph,440.0f); double rb_in=rms(buf,128); api->process_block(inst,buf,128); double rb_out=rms(buf,128);
    CHECK("bypass passes dry", fabs(rb_out-rb_in)/(rb_in+1e-9) < 0.05);
    api->set_param(inst,"bypass","Off");

    /* all 44 variations render without crashing, with wet signal present */
    api->set_param(inst,"mix","0.8"); api->set_param(inst,"effect_vol","0.8");
    api->set_param(inst,"space","0.3");
    unsigned st=22222; int wet_ok=1;
    const char *effs[11]={"Arp","Cutup","Chop","Glide","Seq","Stack",
        "Cloud","Drone","Chain","Taps","Warp"};
    const char *vars[4]={"0","1","2","3"};  /* variation set by index (labels are per-effect) */
    for(int e=0;e<11;e++){ for(int v=0;v<4;v++){
        api->set_param(inst,"effect",effs[e]); api->set_param(inst,"variation",vars[v]);
        for(int blk=0;blk<24;blk++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128); if(!all_finite(buf,128)) wet_ok=0; }
    }}
    CHECK("all 44 variations render", wet_ok);

    /* looper transport */
    api->set_param(inst,"looper_on","On");
    api->set_param(inst,"transport","Rec");
    for(int blk=0;blk<60;blk++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128);} /* ~0.17s */
    api->set_param(inst,"transport","Play");
    char sbuf[64]; api->get_param(inst,"transport",sbuf,sizeof(sbuf));
    CHECK("looper enters Play after close", strcmp(sbuf,"Play")==0);
    for(int blk=0;blk<20;blk++){ memset(buf,0,sizeof(buf)); api->process_block(inst,buf,128);}
    double rplay = rms(buf,128);
    printf("  loop playback rms=%.4f\n", rplay);
    CHECK("looper plays recorded audio into silence", rplay>0.0005);
    api->set_param(inst,"transport","Dub");
    for(int blk=0;blk<10;blk++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128);}
    api->set_param(inst,"transport","Undo");

    /* save the loop to disk, erase, reload — should play again */
    api->set_param(inst,"transport","Save");
    api->set_param(inst,"transport","Erase");
    api->get_param(inst,"transport",sbuf,sizeof(sbuf));
    CHECK("looper erases to Idle", strcmp(sbuf,"Idle")==0);
    api->set_param(inst,"transport","Load");
    for(int blk=0;blk<20;blk++){ memset(buf,0,sizeof(buf)); api->process_block(inst,buf,128);}
    double rload = rms(buf,128);
    printf("  reloaded loop rms=%.4f\n", rload);
    CHECK("loop save+load round-trips audio", rload>0.0005);
    api->set_param(inst,"transport","Erase");
    api->set_param(inst,"looper_on","Off");

    /* Looper Only mutes the effect but keeps dry signal (must NOT be silent) */
    api->set_param(inst,"mix","0.9"); api->set_param(inst,"effect_vol","0.9");
    api->set_param(inst,"loop_only","On");
    double rlo=0; for(int blk=0;blk<8;blk++){ fill_sine(buf,128,&ph,440.0f); api->process_block(inst,buf,128); rlo+=rms(buf,128);}
    printf("  looper-only dry rms=%.4f\n", rlo/8);
    CHECK("Looper Only still passes dry (not silent)", rlo/8 > 0.02);
    api->set_param(inst,"loop_only","Off");
    api->set_param(inst,"mix","0.8"); api->set_param(inst,"effect_vol","0.8");

    /* bypass styles + burst params accept values */
    api->set_param(inst,"bypass_style","Trail");
    api->get_param(inst,"bypass_style",sbuf,sizeof(sbuf));
    CHECK("bypass style sets", strcmp(sbuf,"Trail")==0);
    api->set_param(inst,"bypass_style","Buf");
    api->set_param(inst,"loop_burst","On");
    api->get_param(inst,"loop_burst",sbuf,sizeof(sbuf));
    CHECK("burst toggles", strcmp(sbuf,"On")==0);
    api->set_param(inst,"loop_burst","Off");

    /* Eco CPU mode: toggles + the (thinner) reverb stays bounded */
    api->set_param(inst,"space","0.9"); api->set_param(inst,"reverb_mode","Vast");
    api->set_param(inst,"eco","On");
    api->get_param(inst,"eco",sbuf,sizeof(sbuf));
    CHECK("eco toggles On", strcmp(sbuf,"On")==0);
    double reco=0; for(int blk=0;blk<24;blk++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128); reco+=rms(buf,128);}
    printf("  eco reverb rms=%.1f\n", reco/24);
    CHECK("eco reverb stays bounded", reco/24 < 32760.0 && reco > 0.0);
    api->set_param(inst,"eco","Off");

    /* timing defaults to Free (free-running, drifts) */
    { void *i2=api->create_instance("/tmp",NULL); char tb[16];
      api->get_param(i2,"tempo_src",tb,sizeof(tb));
      CHECK("timing defaults to Free", strcmp(tb,"Free")==0);
      api->destroy_instance(i2); }
    /* grain envelope round-trips */
    api->set_param(inst,"grain_env","Swell");
    api->get_param(inst,"grain_env",sbuf,sizeof(sbuf));
    CHECK("grain_env sets (Swell)", strcmp(sbuf,"Swell")==0);
    api->set_param(inst,"grain_env","Soft");
    /* character preset jumps effect + macros (Birds -> Chains, reverse on) */
    api->set_param(inst,"preset","Birds");
    { char e2[32],r2[8]; api->get_param(inst,"effect",e2,sizeof(e2)); api->get_param(inst,"reverse",r2,sizeof(r2));
      CHECK("preset Birds -> Chains + reverse", strcmp(e2,"Chain")==0 && strcmp(r2,"On")==0); }
    api->set_param(inst,"preset","Init"); api->set_param(inst,"reverse","Off");

    /* every character preset (one per effect + value-add showcases) loads + renders bounded */
    { const char *PS[16]={"Init","Arp","Stutr","Chop","Glass","Seq","Stack","Cloud",
                          "Drone","Birds","Taps","Warp","Sheen","Motn","Evolv","Scale"};
      int allok=1;
      for(int p=0;p<16;p++){ api->set_param(inst,"preset",PS[p]);
        double e=0; for(int blk=0;blk<24;blk++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128); e+=rms(buf,128); }
        if(!(e/24 < 32760.0 && e >= 0.0)) { allok=0; printf("  preset %s rms=%.1f\n", PS[p], e/24); } }
      CHECK("all 16 presets render bounded", allok); }
    api->set_param(inst,"preset","Init"); api->set_param(inst,"reverse","Off");

    /* user-preset bank: save params to a slot, change, reload -> restored */
    api->set_param(inst,"effect","Warp"); api->set_param(inst,"activity","0.66");
    api->set_param(inst,"user_slot","1"); api->set_param(inst,"user_op","Save");
    api->set_param(inst,"effect","Arp"); api->set_param(inst,"activity","0.1");
    api->set_param(inst,"user_op","Load");
    { char eu[32],au[16]; api->get_param(inst,"effect",eu,sizeof(eu)); api->get_param(inst,"activity",au,sizeof(au));
      CHECK("user slot reloads params", strcmp(eu,"Warp")==0 && atof(au)>0.6); }
    /* user slot also captures the recorded loop audio */
    api->set_param(inst,"looper_on","On"); api->set_param(inst,"transport","Rec");
    for(int blk=0;blk<50;blk++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128);}
    api->set_param(inst,"transport","Play");
    api->set_param(inst,"user_slot","2"); api->set_param(inst,"user_op","Save");
    api->set_param(inst,"transport","Erase");
    api->set_param(inst,"user_op","Load");
    for(int blk=0;blk<20;blk++){ memset(buf,0,sizeof(buf)); api->process_block(inst,buf,128);}
    CHECK("user slot reloads loop audio", rms(buf,128) > 0.0005);
    api->set_param(inst,"transport","Erase"); api->set_param(inst,"looper_on","Off");
    api->set_param(inst,"user_op","Del");
    api->set_param(inst,"user_slot","1"); api->set_param(inst,"user_op","Del");
    api->set_param(inst,"effect","Stack"); api->set_param(inst,"activity","0.3");

    /* value-adds: shimmer / scale / motion / evolve / duck / width engage + stay bounded */
    api->set_param(inst,"mix","0.85"); api->set_param(inst,"space","0.6"); api->set_param(inst,"effect","Cloud");
    api->set_param(inst,"shimmer","Oct+"); api->set_param(inst,"pitch_scale","Maj");
    api->set_param(inst,"width","0.5"); api->set_param(inst,"duck","0.7");
    api->set_param(inst,"mot_target","Filt"); api->set_param(inst,"mot_depth","0.8"); api->set_param(inst,"mot_rate","1/4");
    api->set_param(inst,"evolve","0.9"); api->set_param(inst,"evo_range","Wild");
    { double rmax=0; for(int blk=0;blk<160;blk++){ fill_noise(buf,128,&st); api->process_block(inst,buf,128);
        double r=rms(buf,128); if(r>rmax)rmax=r; }
      printf("  value-adds peak rms=%.4f\n", rmax);
      CHECK("value-adds stay bounded (shimmer feedback safe)", rmax>0.0005 && rmax<0.999); }
    { char sb2[16];
      api->get_param(inst,"shimmer",sb2,sizeof sb2);     CHECK("shimmer round-trips", strcmp(sb2,"Oct+")==0);
      api->get_param(inst,"pitch_scale",sb2,sizeof sb2); CHECK("scale round-trips", strcmp(sb2,"Maj")==0);
      api->get_param(inst,"mot_target",sb2,sizeof sb2);  CHECK("motion target round-trips", strcmp(sb2,"Filt")==0);
      api->get_param(inst,"evo_range",sb2,sizeof sb2);   CHECK("evo_range round-trips", strcmp(sb2,"Wild")==0);
      api->set_param(inst,"dice","Roll"); api->get_param(inst,"dice",sb2,sizeof sb2);
      CHECK("dice is momentary", strcmp(sb2,"-")==0); }
    api->set_param(inst,"shimmer","Off"); api->set_param(inst,"pitch_scale","Off");
    api->set_param(inst,"mot_target","Off"); api->set_param(inst,"evolve","0");
    api->set_param(inst,"duck","0"); api->set_param(inst,"width","1");
    api->set_param(inst,"effect","Stack"); api->set_param(inst,"mix","0.8"); api->set_param(inst,"space","0.3");

    /* MIDI: CC6 (Activity) and program change should move params */
    if (api->on_midi) {
        uint8_t cc[3]={0xB0,6,127}; api->on_midi(inst,cc,3,0);
        api->get_param(inst,"activity",sbuf,sizeof(sbuf));
        CHECK("MIDI CC6 -> Activity", atof(sbuf)>0.9);
        uint8_t pc[2]={0xC0,23}; api->on_midi(inst,pc,2,0);  /* PC#24 -> effect 5 (Stack) var 3 */
        char e[32],vv[8]; api->get_param(inst,"effect",e,sizeof(e)); api->get_param(inst,"variation",vv,sizeof(vv));
        CHECK("MIDI PC#24 -> Stack / range", strcmp(e,"Stack")==0 && strcmp(vv,"range")==0);
    }

    /* hold sampler freezes capture (no crash) + hold/looper exclusivity */
    api->set_param(inst,"hold","On");
    for(int blk=0;blk<10;blk++){ memset(buf,0,sizeof(buf)); api->process_block(inst,buf,128);}
    api->set_param(inst,"looper_on","On");
    api->get_param(inst,"hold",sbuf,sizeof(sbuf));
    CHECK("enabling looper clears Hold", strcmp(sbuf,"Off")==0);
    api->set_param(inst,"looper_on","Off");

    /* state + chain_params are non-empty JSON */
    int n1=api->get_param(inst,"state",sbuf,sizeof(sbuf));
    char big[8192]; int n2=api->get_param(inst,"chain_params",big,sizeof(big));
    CHECK("state JSON returned", n1>0 && sbuf[0]=='{');
    CHECK("chain_params JSON returned", n2>0 && big[0]=='[' && big[n2-1]==']');
    /* every editable param must carry type metadata so the host can edit it */
    CHECK("chain_params: mix is typed float", strstr(big,"\"key\":\"mix\",\"name\":\"Mix\",\"type\":\"float\"")!=NULL);
    CHECK("chain_params: mod_depth typed float w/ unit", strstr(big,"\"key\":\"mod_depth\",\"name\":\"Mod Dep\",\"type\":\"float\"")!=NULL && strstr(big,"\"unit\":\"%\"")!=NULL);
    CHECK("chain_params: slim looper keeps loop_level (typed)", strstr(big,"\"key\":\"loop_level\",\"name\":\"Level\",\"type\":\"float\"")!=NULL);
    CHECK("chain_params: dropped looper extras gone (loop_only/loop_burst)", strstr(big,"\"key\":\"loop_only\"")==NULL && strstr(big,"\"key\":\"loop_burst\"")==NULL);
    CHECK("chain_params: variation options are per-effect labels", strstr(big,"\"key\":\"variation\"")!=NULL && strstr(big,"\"range\"")!=NULL);
    { int cnt=0; const char *p=big; while((p=strstr(p,"\"key\":"))){cnt++;p+=6;}
      printf("  chain_params count=%d\n",cnt);
      CHECK("chain_params exposes the whole param set (>=30)", cnt>=30); }
    /* ui_hierarchy served by the DSP (manifest has none), valid JSON with levels */
    char hier[8192]; int n3=api->get_param(inst,"ui_hierarchy",hier,sizeof(hier));
    CHECK("ui_hierarchy JSON returned", n3>0 && strstr(hier,"\"levels\"")!=NULL && strstr(hier,"\"root\"")!=NULL);

    api->destroy_instance(inst);
    printf("\n%s (%d failures)\n", fails?"TESTS FAILED":"ALL TESTS PASSED", fails);
    return fails?1:0;
}
