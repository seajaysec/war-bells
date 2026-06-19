/* wb_metrics.h — reusable, scored audio-quality probes for War Bells.
 *
 * The point (per the user's ask): every artifact KIND is one reusable probe that returns a NUMERIC SCORE,
 * not a single pass/fail. A driver feeds engine output through these, builds a per-case x per-metric
 * scorecard, and tracks each metric over time vs a committed baseline. Add a new case → get every metric
 * for free. New modules can reuse this header verbatim.
 *
 * Two layers:
 *   1. wb_acc  — streaming time-domain accumulator (feed normalized stereo block by block):
 *                silence, runaway, true-peak, discontinuity/clicks, DC offset, non-finite.
 *   2. wb_fft + wb_spectral — spectral probes for a captured mono tone tail:
 *                harshness (HF energy ratio) and aliasing/THD (inharmonic energy ratio).
 * Denormal-stall is timed by the driver (CPU per block on decay-to-silence), since it's not a property
 * of the samples. All scores are "higher = worse" unless noted.
 */
#ifndef WB_METRICS_H
#define WB_METRICS_H
#include <math.h>
#include <string.h>

/* ---------------- time-domain accumulator ---------------- */
typedef struct {
    long   n;                 /* samples seen (per channel, mono-summed) */
    double sumsq, sum;        /* for rms, dc */
    double peak, truepeak;    /* sample peak, inter-sample (2x) peak */
    double p1;                /* prev mono sample (true-peak interp) */
    double d1, d2;            /* prev two mono samples (2nd-difference click detector) */
    double d2mean;            /* running mean of |2nd diff| — clicks are OUTLIERS vs this */
    long   clicks;            /* count of discontinuity outliers (clicks/zipper, not broadband noise) */
    double click_thr;
    long   early_lim, late_start;
    double early_sumsq; long early_n;
    double late_sumsq;  long late_n; double late_peak;
    int    nonfinite;
} wb_acc;

static void wb_acc_init(wb_acc *a, long total_samples) {
    memset(a, 0, sizeof(*a));
    a->click_thr  = 0.15;                  /* floor for the outlier detector (ignore near-silence jitter) */
    a->early_lim   = total_samples / 10;   /* first 10% (fill/onset) */
    a->late_start  = total_samples * 9 / 10; /* last 10% (steady state) */
}
static void wb_acc_feed(wb_acc *a, const float *L, const float *R, int n) {
    for (int i = 0; i < n; i++) {
        double l = L[i], r = R[i], m = 0.5 * (l + r);
        if (!(l > -8.0 && l < 8.0) || !(r > -8.0 && r < 8.0)) a->nonfinite++;
        double al = fabs(l), ar = fabs(r), am = al > ar ? al : ar;
        if (am > a->peak) a->peak = am;
        double mid = fabs(0.5 * (a->p1 + m));        /* 2x linear inter-sample peak proxy */
        if (mid > a->truepeak) a->truepeak = mid;
        if (am  > a->truepeak) a->truepeak = am;
        double d2 = fabs(m - 2.0 * a->d1 + a->d2);   /* 2nd difference spikes on clicks/clamp folds */
        /* A click is an OUTLIER: 2nd-diff far above the local average (broadband noise has a large but
         * STEADY 2nd-diff, so it doesn't trip this; a hard jump/clamp-fold does). Floor avoids silence. */
        double cthr = 8.0 * a->d2mean; if (cthr < a->click_thr) cthr = a->click_thr;
        if (a->n > 64 && d2 > cthr) a->clicks++;
        a->d2mean += (d2 - a->d2mean) * 0.001;       /* ~1000-sample running mean */
        a->d2 = a->d1; a->d1 = m; a->p1 = m;
        a->sumsq += m * m; a->sum += m; a->n++;
        if (a->n <= a->early_lim) { a->early_sumsq += m * m; a->early_n++; }
        if (a->n >= a->late_start) { a->late_sumsq += m * m; a->late_n++; if (am > a->late_peak) a->late_peak = am; }
    }
}
static double wb_rms(const wb_acc *a)        { return a->n ? sqrt(a->sumsq / a->n) : 0.0; }
static double wb_dc(const wb_acc *a)         { return a->n ? fabs(a->sum / a->n) : 0.0; }
static inline double wb_late_rms(const wb_acc *a) { return a->late_n ? sqrt(a->late_sumsq / a->late_n) : 0.0; }
static double wb_click_rate(const wb_acc *a) { return a->n ? (double)a->clicks * 44100.0 / (double)a->n : 0.0; } /* clicks/sec */

/* ---------------- spectral probes (radix-2 FFT) ---------------- */
static void wb_fft(float *re, float *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len; float wr = (float)cos(ang), wi = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                float ur = re[i + k], ui = im[i + k];
                float vr = re[i + k + len/2] * cr - im[i + k + len/2] * ci;
                float vi = re[i + k + len/2] * ci + im[i + k + len/2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len/2] = ur - vr; im[i + k + len/2] = ui - vi;
                float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}
/* Analyze a captured mono tone tail (cap[0..N), N power of two) driven by a pure sine at f0.
 * Returns: *thd  = inharmonic energy / total (aliasing + distortion that isn't a harmonic of f0),
 *          *hf   = energy above 6 kHz / total (harshness). Both 0..1, higher = worse. */
static void wb_spectral(const float *cap, int N, double f0, double sr, double *thd, double *hf) {
    static float re[16384], im[16384];
    if (N > 16384) N = 16384;
    for (int i = 0; i < N; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / (N - 1));   /* Hann */
        re[i] = (float)(cap[i] * w); im[i] = 0.0f;
    }
    wb_fft(re, im, N);
    double total = 0, high = 0, hi_inharm = 0;
    double binhz = sr / N;
    int half = N / 2;
    int hf_bin = (int)(6000.0 / binhz);     /* harshness band */
    int al_bin = (int)(4000.0 / binhz);     /* aliasing band: above the first ~18 harmonics of 220 Hz */
    for (int b = 1; b < half; b++) {
        double mag = (double)re[b] * re[b] + (double)im[b] * im[b];
        total += mag;
        if (b >= hf_bin) high += mag;
        if (b >= al_bin) {
            double hz = b * binhz, nearest = floor(hz / f0 + 0.5);
            if (!(nearest >= 1.0 && fabs(hz - nearest * f0) <= 2.0 * binhz)) hi_inharm += mag;  /* HF energy that ISN'T a harmonic = aliasing/imaging */
        }
    }
    /* Both relative to TOTAL energy: a clean tone (even through a big reverb, whose modal energy sits
     * low) reads near zero; real HF aliasing/harshness pushes these up. Low-frequency reverb density no
     * longer counts as "aliasing". */
    *thd = total > 1e-12 ? hi_inharm / total : 0.0;
    *hf  = total > 1e-12 ? high / total : 0.0;
}

/* ---------------- scorecard plumbing ---------------- */
typedef struct { const char *name; double score; double threshold; int higher_worse; int pass; } wb_metric;
static int wb_metric_set(wb_metric *m, const char *name, double score, double threshold, int higher_worse) {
    m->name = name; m->score = score; m->threshold = threshold; m->higher_worse = higher_worse;
    m->pass = higher_worse ? (score <= threshold) : (score >= threshold);
    return m->pass;
}
#endif /* WB_METRICS_H */
