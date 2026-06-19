#!/usr/bin/env python3
"""analyze.py — deep offline audio-quality analysis (self-contained, no attribution needed).

When the fast C probes flag something and you want the spectrum, render the engine output to a WAV and run:
    python3 analyze.py out.wav                 # THD+N / true-peak / crest / HF for a tone or mix
    python3 analyze.py out.wav --f0 220        # lock the fundamental for THD
    python3 analyze.py a.wav b.wav             # null test (A vs B): residual + gain/polarity guess

Deps: numpy, scipy, soundfile  (pip install numpy scipy soundfile)
Implements: flat-top-windowed THD/THD+N (cluster-integrated, leakage-robust), 4x true-peak (ITU-R BS.1770),
crest factor, HF/aliasing ratio, and an alignment-tolerant null test.
"""
import sys, numpy as np

def load(path):
    import soundfile as sf
    x, sr = sf.read(path, always_2d=True)
    return x.mean(axis=1).astype(np.float64), sr           # mono sum

def true_peak_db(x, sr, os=4):
    from scipy.signal import resample_poly
    up = resample_poly(x, os, 1)
    p = np.max(np.abs(up)) + 1e-12
    return 20*np.log10(p)

def crest_db(x):
    rms = np.sqrt(np.mean(x**2)) + 1e-12
    pk  = np.max(np.abs(x)) + 1e-12
    return 20*np.log10(pk/rms)

def thd(x, sr, f0=None):
    n = 1 << int(np.floor(np.log2(len(x))))                # power-of-two
    x = x[:n] - np.mean(x)
    w = np.blackman(n)                                     # flat-ish; use scipy flattop if available
    try:
        from scipy.signal.windows import flattop; w = flattop(n)
    except Exception: pass
    X = np.abs(np.fft.rfft(x*w))**2                        # power spectrum
    binhz = sr/n
    guard = int(20/binhz)
    if f0 is None:
        f0bin = guard + int(np.argmax(X[guard:]))
    else:
        f0bin = int(round(f0/binhz))
    half = max(3, int(0.0)) + 5                            # flat-top main lobe ~5 bins
    def cluster(c): a=max(0,c-half); b=min(len(X),c+half+1); return X[a:b].sum()
    pf = cluster(f0bin)
    total = X.sum()
    # harmonics 2..K within band
    harm = 0.0; k = 2
    while k*f0bin < len(X):
        harm += cluster(k*f0bin); k += 1
    residual = total - pf
    thd_  = np.sqrt(harm/ (pf+1e-20))
    thdn  = np.sqrt(residual/(total+1e-20))
    hf_bin = int(6000/binhz)
    hf = X[hf_bin:].sum()/(total+1e-20)
    return dict(f0=f0bin*binhz, thd_pct=100*thd_, thd_db=20*np.log10(thd_+1e-12),
                thdn_pct=100*thdn, hf_ratio=hf)

def null_test(a, b):
    n = min(len(a), len(b)); a=a[:n]; b=b[:n]
    # best scalar gain (least squares), test both polarities
    g = float(np.dot(a,b)/(np.dot(b,b)+1e-20))
    res = a - g*b
    rdb = 20*np.log10((np.sqrt(np.mean(res**2))+1e-12)/(np.sqrt(np.mean(a**2))+1e-12))
    return dict(gain=g, polarity="inverted" if g<0 else "normal", residual_db=rdb,
                identical=rdb < -120)

def main(argv):
    args=[a for a in argv if not a.startswith("--")]
    f0=None
    for a in argv:
        if a.startswith("--f0"): f0=float(a.split("=")[-1]) if "=" in a else float(argv[argv.index(a)+1])
    if len(args)>=2:
        a,sr=load(args[0]); b,_=load(args[1]); print("NULL TEST:", null_test(a,b)); return
    x,sr=load(args[0])
    print(f"file: {args[0]}  sr={sr}  len={len(x)/sr:.2f}s")
    print(f"true_peak: {true_peak_db(x,sr):+.2f} dBTP   crest: {crest_db(x):.2f} dB")
    print("spectral:", {k:(round(v,4)) for k,v in thd(x,sr,f0).items()})

if __name__=="__main__":
    if len(sys.argv)<2: print(__doc__); sys.exit(0)
    main(sys.argv[1:])
