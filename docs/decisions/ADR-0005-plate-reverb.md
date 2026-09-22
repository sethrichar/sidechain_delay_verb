# ADR-0005 — Plate reverb: Dattorro tank, decay calibration, size coupling, tail, wrapper stages

**Status:** accepted (Phase 3, 2026-09-22)

## Decisions

### The algorithm is the paper's, lengths scaled from 29.761 kHz, read with Hermite

`PlateReverb` is J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", JAES
45(9), 1997, Figure 1 and Table 2, implemented from the paper (CLAUDE.md §4): four series
input diffusion allpasses (142 / 107 / 379 / 277 samples, 0.75 / 0.75 / 0.625 / 0.625), then a
tank of two cross-coupled halves — modulated allpass (672 | 908, −0.7) → delay (4453 | 4217)
→ one-pole damping → × decay → allpass (1800 | 2656, +0.5) → delay (3720 | 3163) → × decay →
the other half — with the paper's seven output taps per side. Every length is the paper's
sample count × fs / 29761, so the plate sounds the same at 44.1, 48 and 96 kHz. Tank
elements are read with the Hermite `DelayLine` because their lengths are fractional at every
rate and the size control moves them continuously; the input diffusers are rounded to
integer samples (the ±0.5-sample error on a 4–13 ms allpass is inaudible) and read without
interpolation.

The paper's input "bandwidth" LPF (coefficient 0.9995, i.e. practically open) is not built;
`ReverbEngine`'s high cut on the return covers that ground with a real control.

### Decay: loop gain from the round trip, calibrated to the measured RT60

The decay gain is applied four times per figure-eight round trip, so with the loop time
`L(size)` = (sum of all eight tank lengths) × size scale / 29761 s (0.725 s at the default
size) the gain per stage is

    g = 10^(−3 · (L/4) · decayCalibration / RT60)

Allpasses inside the loop have a frequency-dependent group delay (mean = their length, but
up to 5.7× longer at their pole peaks), so the modes with the longest group delay decay
slowest and a broadband Schroeder fit reads somewhat off the mean-based prediction.
`decayCalibration` = **0.92** was set from the hidden `[.calibrate]` table in
`tests/test_reverb.cpp` at the 6 kHz damping default; with it the measured RT60 is within
+12 % / −7 % of `reverbDecay` for 0.3–20 s at every size (Phase 3 acceptance is ±15 %).

Two things are deliberately *not* compensated:
- **Damping** shortens the highs by design, so a dark plate reads shorter on a broadband fit
  (1.6 s at 1 kHz damping for a 2 s setting). `reverbDecay` is the RT60 of the band the
  damping does not reach; the test checks that the < 300 Hz band decays at the same rate
  whether damping is open or closed.
- **The low band rings ~20 % longer** than the broadband figure at every setting (2.4 s for
  a 2 s setting). That is the tank's character (the long-group-delay modes are the low ones);
  calibrating to the low band instead would make every plate read short on the broadband
  measure the acceptance criteria specify.

### Allpass coefficients are capped for short decays

An allpass of length N with coefficient k rings for 3N / −log10|k| seconds to −60 dB: the
2656-sample tank allpass at 0.5 rings ~0.9 s, the 672 at 0.7 ~0.44 s. With a short decay
those tails outlast the loop, and cascaded stages decaying at similar rates read long on the
Schroeder fit (the first cut measured 0.3 s settings at 0.34–0.37 s). Every allpass therefore
uses `min(|k_nominal|, 10^(−3N / (0.5·RT60)))` — its ring-out is capped at **half** the
requested decay. Nothing changes above ~1.8 s at full size; short plates lose some
diffusion, which is what a short plate needs anyway.

### Short decays shrink the tank (`maxLoopToDecayRatio` = 1.2)

A 0.3 s decay on a tank whose round trip is 1.1 s (size 100 %) is not a decay — the impulse
is still arriving at the last tap when it should be 60 dB down — and it measured at 0.19 s.
The size scale actually used is therefore limited so that `L(size)` ≤ 1.2 × decay (never
below the 0.5× minimum). The Size knob is unaffected at decays above ~0.9 s; below that its
upper range is progressively limited (at 0.3 s every size is the smallest tank). Alternatives
rejected: leaving it (fails the acceptance and sounds like early reflections), or moving the
output taps toward the tank input at short decays (changes the plate's character and the
paper's tap set).

### Size glides over 300 ms

`reverbSize` moves every tank read position, tap included. The positions are slewed over
300 ms rather than crossfaded: a crossfade would need a second set of 22 Hermite reads and,
unlike the Digital delay, a reverb has no "pitch" to preserve — a slow stretch of the plate
is the accepted behaviour of plate plugins. The decay gain and the allpass caps are
recomputed from the *current* (gliding) size every block, so RT60 stays put while the tank
stretches. A 0 → 100 % jump on a 200 Hz tone measured a maximum sample step under 3× the
steady tone's own (the click test in `tests/test_reverb.cpp`).

### Modulation: one sine per half, detuned

Each half's first allpass is modulated by its own sine LFO (`reverbModRate`), the right half
at 1.07× the rate (SPEC §5.1's "second, slightly detuned LFO"), starting in quadrature, so
the two sides never move in lockstep. Excursion is `reverbModDepth` × 16 samples at
29.761 kHz (the paper's figure; ~8 samples at 48 kHz at the 30 % default). Depth and rate
changes are smoothed (20 ms) and the phase is continuous, so modulation changes are
click-free.

### Metallic-ringing check is a periodicity measure, not a single-lag autocorrelation

The plate's output taps read one delay line at two points (e.g. 266 and 2974 on the right
half's first delay for the left output), and the same signal appears later at taps further
down the half. That gives the impulse response a few single echo pairs at fixed spacings
(normalised autocorrelation ≈ 0.2 at 45–110 ms) — the paper's design, and what makes a plate
dense rather than a lone echo. A *ring* repeats at a period **and its multiples**, so the
acceptance test scores max over τ of min(r(τ), r(2τ)) and requires < 0.1 (−20 dB), with the
plain single-lag maximum bounded at 0.35. The test proves the measure on a synthetic comb, a
four-comb bank at the Phase 1 stand-in's periods, and a lone echo pair.

### Tail = pre-delay + round trip + 1.25 × decay

At a large size the impulse needs most of a round trip just to reach the last tap, and the
long-group-delay modes decay slower than the fitted RT60, so `getTailLengthSeconds` reports
pre-delay + `L(effective size)` + 1.25 × decay. Measured: ≥ 60 dB down at the reported tail
for 0.3–10 s at every size (the test uses the 250 ms pre-delay maximum). Overestimating a tail
only costs a host a little extra rendering.

### Wrapper stages in `ReverbEngine`

- **Pre-delay**: a stereo `DelayLine` (so the Phase 4 Hall, which is stereo-in, keeps its
  image) with the read position glided over 100 ms; the read is continuous through 0 (direct
  → linear → Hermite) so a glide through zero never steps. Range 0–250 ms; measured exact to
  the sample at 44.1 / 48 / 96 kHz.
- **Low / high cut**: 2nd-order Butterworth `Biquad`s on the return, coefficients recomputed
  once per block along a 20 ms multiplicative ramp (same scheme as the delay's tone filters).
- **Width**: mid/side, side × width, 20 ms ramp. 0 % is exactly mono (L = R); 100 % is the
  plate's own stereo, which measures |correlation| < 0.5 (acceptance) at 0.1 on a 2 s IR.
- **Modes**: Hall and Room run the Plate until Phase 4; Room already clamps the decay to
  2.5 s (SPEC §6) so presets saved now behave the same later.

### Input is summed to mono

The Dattorro tank is mono-in / stereo-out. `PlateReverb` feeds 0.5 × (L + R). A stereo
source loses its left/right placement inside the plate (a real plate does the same); the
Phase 4 Hall FDN is stereo-in.

### Output level

The paper's 0.6 tap sum is scaled by a further 0.5. Measured at 100 % wet, 48 kHz: a 2 s
plate returns a sustained 1 kHz tone at −2.4 dB and noise at −7.7 dB relative RMS; the wet
peak stays within +6 dB of the input peak at the default decay. A 3 s tone into a 20 s plate
builds up to +10 dB over the input peak — energy storage, not a fault — and stays bounded.
Whether this sits right on a return is a listening question (CHANGELOG v0.4).

## Alternatives considered

- **Freeverb / Schroeder comb banks**: rejected by SPEC §5.1 (and by the sound of the Phase 1
  stand-in).
- **An FDN for the plate**: that is the Phase 4 Hall; SPEC asks for the Dattorro lineage here.
- **Calibrating the decay per damping setting**: rejected; it would make the Decay knob's
  meaning drift with Damping, and low-band decay is what players hear as "the decay".
- **Hard-clamping size at short decays without smoothing**: rejected; the coupling goes
  through the same 300 ms glide as the knob.
- **Crossfading two tanks on size changes**: rejected for cost and because the stretch is the
  expected behaviour; revisit only if the glide reads badly in listening.
