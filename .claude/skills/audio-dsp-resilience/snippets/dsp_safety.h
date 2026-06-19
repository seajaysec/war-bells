/* dsp_safety.h — drop-in real-time-safe DSP helpers (self-contained, public/no-attribution).
 * Generic `dsp_` prefix; copy into any project. Pairs with audio-dsp-resilience/references/techniques.md. */
#ifndef DSP_SAFETY_H
#define DSP_SAFETY_H
#include <stdint.h>
#include <math.h>

/* --- 1. Denormal flush: call once per audio block, on the audio thread. --- */
static inline void dsp_flush_denormals(void) {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int c; __asm__ volatile("stmxcsr %0":"=m"(c)); c|=0x8040u; /* FTZ|DAZ */ __asm__ volatile("ldmxcsr %0"::"m"(c));
#elif defined(__aarch64__)
    uint64_t f; __asm__ volatile("mrs %0, fpcr":"=r"(f)); f|=(1ULL<<24); /* FZ */ __asm__ volatile("msr fpcr, %0"::"r"(f));
#endif
}

/* --- 2. Soft saturation (bounded, click-free) — use INSTEAD of hard clamps in feedback loops. --- */
static inline float dsp_softclip(float x){            /* cubic, ~±0.94 ceiling, cheap */
    if (x >  1.2f) x =  1.2f; else if (x < -1.2f) x = -1.2f;
    return x - 0.148148f*x*x*x;                        /* x - x^3/6.75 */
}
static inline float dsp_tanh_sat(float x){ return tanhf(x); }  /* smoother; pricier */

/* --- 3. DC blocker (one-pole HPF): y = x - x1 + R*y1.  R=0.9995 ≈ 3.5 Hz @44.1k. --- */
typedef struct { float x1, y1; } dsp_dcblock_t;
static inline float dsp_dcblock(dsp_dcblock_t *s, float x, float R){
    float y = x - s->x1 + R*s->y1; s->x1 = x; s->y1 = y; return y;
}

/* --- 4. Per-sample parameter smoother (no zipper). k from dsp_smooth_k(tau, fs). --- */
static inline float dsp_smooth_k(float tau_sec, float fs){ return 1.0f - expf(-1.0f/(tau_sec*fs)); }
static inline float dsp_smooth(float cur, float target, float k){ return cur + (target - cur)*k; }

/* --- 5. dB <-> linear (smooth gains in dB, convert after). --- */
static inline float dsp_db2lin(float db){ return powf(10.0f, db*0.05f); }
static inline float dsp_lin2db(float a){ return 20.0f*log10f(a > 1e-9f ? a : 1e-9f); }

/* --- 6. Reverb/loop feedback from an RT60 target (bounded by design). --- */
static inline float dsp_rt60_feedback(float delay_samples, float fs, float rt60_sec){
    return powf(10.0f, -3.0f * delay_samples / (fs * rt60_sec));
}

/* --- 7. ADAA 1st-order for a memoryless nonlinearity f with antiderivative F (anti-alias w/o oversampling).
 *        Pass current/previous input; guard the tiny-denominator case. --- */
static inline float dsp_adaa1(float x, float x1, float (*F)(float), float (*f)(float)){
    float dx = x - x1;
    if (fabsf(dx) < 1e-5f) return f(0.5f*(x+x1));     /* fall back to midpoint */
    return (F(x) - F(x1)) / dx;
}
#endif /* DSP_SAFETY_H */
