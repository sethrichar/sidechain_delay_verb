# ADR-0004 — Digital delay: time changes, feedback clamp, tone filters, modulation, ping-pong, sync

**Status:** accepted (Phase 2, 2026-09-17)

## Decisions

### Time changes: two read heads on one buffer, 30 ms equal-power crossfade

SPEC §4 asks for "two delay lines and crossfade" in Digital mode. `DigitalDelay` uses one write
buffer per channel with **two read heads**: on a time change the idle head is set to the new
time and faded in over `crossfadeMs` (30 ms, sin/cos law, advanced by a rotating phasor so
there is no trig per sample) while the sounding head fades out. Both heads read the same
written history, which is exactly what two lines fed by the same input (and the same feedback)
would hold, at half the memory. The feedback signal is the crossfaded output, so the loop
never sees a jump.

A time change that arrives **while a crossfade is running is queued** and starts its own
crossfade when the current one ends. A continuous knob sweep therefore steps every 30 ms,
each step click-free. This is the classic digital-delay behaviour (the pitch-glide belongs to
BBD/Tape, SPEC §4). The alternative — retargeting the incoming head mid-fade — was rejected:
it either restarts the fade (audible level dip) or needs a third head.

`reset()` (used when the section is bypassed, ADR-0003) snaps both heads to the requested
time, so un-bypassing never crossfades from a stale time.

### Feedback: 100 % on the knob = 0.98 in the loop

`delayFeedback` 0–100 % maps linearly to a loop coefficient 0–`maxFeedback` = **0.98**, the
same ceiling the Phase 1 stand-in used. With a unity-gain loop and the tone filters wide open,
a coefficient of 1.0 would let a sustained input build up by ~+68 dB (the loop only loses the
filters' passband ripple per pass), which is dangerous live. 0.98 bounds the build-up at
1/(1 − 0.98) = 50× (+34 dB) on a steady tone, and a stopped input decays by 60 dB in ~342
repeats. There is deliberately no saturation or limiter in Digital mode (SPEC §4.1); if the
+34 dB worst case is still too hot in practice, lowering `maxFeedback` is a one-line change and
the level-sanity test (`delay: repeats follow the feedback amount…`) pins the bound.

### Tone filters sit in the feedback path only

`delayLowCut` / `delayHighCut` (2nd-order Butterworth `Biquad`s) filter what is written back,
not the return. The first echo is therefore an exact copy of the input and every further
repeat is shaped once more — SPEC §4.1's "repeats can be shaped". Cutoffs ramp over 20 ms
(multiplicative smoothing) but the coefficients are recomputed **once per block** when the
value moved; at practical block sizes the steps are far below audibility and it keeps the
per-sample loop free of trig.

### Modulation: sine on the read position, L/R in quadrature, 2 ms maximum

`delayMod` 0–100 % is 0–`maxModMs` = 2 ms of read-position excursion at `delayModRate`
(SPEC §4.1). Left uses sin, right uses cos of the same phase, so a mono source gets a gentle
stereo movement rather than L and R pitching together. Both read heads carry the same offset
so a crossfade and the LFO never fight. Peak pitch deviation is 2π·rate·depth (2.5 % at
2 Hz / 100 %; 0.1 % at the 0.8 Hz / 10 % defaults), which the modulation test measures from
zero crossings. Because the modulation lowers the read position, the effective minimum delay
is `DelayLine::minHermiteDelay` (2 samples): the interpolator needs one sample newer than the
read point, and reads are relative to the *next* write.

### Ping-pong: mono sum into the left line, cross-fed feedback

Stereo mode = two independent lines with the same time. PingPong = the mono sum of the input
enters the **left** line only; the left return (filtered, × feedback) feeds the right line and
the right return feeds the left. Successive echoes therefore alternate L, R, L, … starting on
the left — the behaviour most ping-pong delays have (and what the acceptance test checks).
Feeding both lines and only cross-feeding was rejected: with a mono source both lines carry
the same signal and nothing alternates. Switching modes is a 20 ms blend of the two
topologies (`pingPongBlend`), not a jump, so it is click-free mid-performance.

### Tempo sync

`delaySync` on → time = note length at the host BPM (`DelayEngine::noteLengthSeconds`),
clamped to the mode's range, so a whole note at 60 BPM lands on the 2 s maximum rather than
wrapping or failing. The BPM is read from the playhead **every block, playing or stopped**
(a stopped transport still reports its tempo, which is what you want when auditioning), and
falls back to **120 BPM** when there is no playhead or no tempo (Standalone). The playhead
is only touched in `processBlock`; `prepareToPlay` uses the last cached tempo.

`getTailLengthSeconds()` uses the last effective time (`getEffectiveDelayTimeMs()`, an atomic
the audio thread writes), so it is correct for synced times too.

### Tail

`(time + 2 ms) × (1 + repeats to −60 dB)` with the clamped feedback, so the modulation's
2 ms allowance is always included. At 100 % feedback and 2 s this is honest but long
(~11 min); hosts that render tails will simply keep going until −60 dB.

### BBD / Tape modes before Phase 5

`DelayEngine` already applies the per-mode time clamps from SPEC §6 (BBD 20–1000 ms, Tape
20–2000 ms) and runs the Digital delay for all three modes. Phase 5 replaces the BBD/Tape
paths and adds the 50 ms mode crossfade; nothing about parameters or state changes.

## Alternatives considered

- Slewing the read position in Digital mode (one line, no crossfade): rejected by SPEC §4 —
  that is the BBD/Tape character.
- Linear crossfade instead of equal-power: rejected; two differently delayed copies of a
  sustained source are decorrelated, so a linear fade dips by up to −3 dB in the middle.
- Feedback ceiling of 1.0 with a soft limiter in the loop: rejected for Digital (SPEC says no
  saturation); revisit only if 0.98 proves musically limiting.
- Linear interpolation instead of Hermite: rejected; SPEC §4 specifies cubic Hermite, and
  linear interpolation's high-frequency loss changes with the fractional part (audible as
  tone wobble under modulation).
