# Fidelity guard — change resilience without changing the sound

The mandate is usually "fix the breakage but don't touch the sound." Prove it; don't assume it.

## A/B render guard
Keep a tiny harness (`bench_ab`-style) that renders **identical seeded input** through a fixed set of configs
and prints peak + RMS (and ideally a spectral/THD diff). Run it before vs after a change:
- **Unaffected configs must stay bit-similar.** If a config that doesn't use the path you changed moves at all,
  you changed something you didn't mean to.
- **Affected configs should change in the intended direction only** (e.g. the runaway path now bounded; the DC
  gone). Expect them to change — verify the change is *only* what you intended.
- Gate clean-path identity in CI: `assert bit-identical` for the configs your change shouldn't touch.

## Keep "off" paths bit-identical
When adding a feature/safety, gate it so the neutral setting is the *exact* original code path (e.g. warp=0.5
and sustain=0 → the original delay, byte-for-byte). New behavior only when the new control is engaged. This
preserves every existing preset and makes A/B trivial.

## Beware meter-vs-ear divergence
RMS/peak include inaudible content (DC, subsonics). After a DC/denormal fix the meters drop but loudness
doesn't. Judge loudness by ear or a DC-blocked/A-weighted measure; judge *correctness* by the artifact probes.

## Verify on the surface the user actually hears
The bug may only reproduce on the real playback surface (the device, the web demo with its real source) and
not in the host test (different input, different preset values). Reproduce where the user is, then fix, then
re-verify there. (Classic miss: host tests green while the web demo crashed — different preset source.)

## Loudness is a knob, not a fix
If something is genuinely too quiet after a correct fix, restore level with an explicit, bounded makeup (pre-
limiter) or per-preset levels — never by loosening a safety bound. And confirm by ear; loudness re-balancing
wants human ears, so surface it as a decision rather than guessing.
