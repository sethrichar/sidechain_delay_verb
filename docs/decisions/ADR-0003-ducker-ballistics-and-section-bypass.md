# ADR-0003 — Ducker timing definitions, hold semantics, and section bypass behaviour

**Status:** accepted (Phase 1, 2026-09-17)

## Decisions

### Attack / Release = time to complete 95 % of the move

`Ducker` smooths the gain reduction (in dB) with one-pole filters (Giannoulis et al.
2012, log-domain, branching). The user-facing `duckAttack` / `duckRelease` values are the
time for the gain reduction to travel **95 % of the way** to its target, i.e. the one-pole
time constant is `time / 3` (`Ducker::coefficientFor95PercentIn`).

Why: CLAUDE.md §6 asks that the return "reaches Depth (±1 dB) within Attack" and "recovers
to −1 dB of unity within Release". With the default 12 dB depth, ±1 dB is ~92 % of the
move, so a definition based on one time constant (63 %) would fail the spec by a factor
of ~2.5, while a 99 % definition makes the knobs feel unusually fast. 95 % passes the spec
at every depth up to 20 dB and still reads like a compressor.

Measured at 48 kHz, defaults (5 / 60 / 400 ms, depth 12, −6 dBFS 1 kHz key):
attack 4.25 ms (5 % → depth−1 dB), hold 81 ms, release 326 ms (release start → 1 dB).

### Hold starts when the *computed* gain reduction drops below the *smoothed* one

The hold timer is (re)armed on every sample where the gain computer asks for more
reduction than is currently applied, and counts down once it asks for less. A re-trigger
during hold or release re-arms the attack and the hold. Consequence: with a hot key the
8 ms RMS detector needs ~20 ms after the key stops before the computed reduction falls
below Depth, so the effective hold is `duckHold + ~20 ms`. This is documented rather than
compensated: the extra time is proportional to how far above threshold the key was, and
compensating it would make `hold = 0` release *before* the detector has actually seen the
key stop.

### Ducker disabled = 20 ms fade to unity, not the release time

Toggling `duckEnable` off uses a fixed 20 ms one-pole toward 0 dB rather than the release
curve, so a 3 s release does not leave the return ducked for 3 s after the user switched
ducking off. Re-enabling uses the normal attack.

### Section bypass: 20 ms fade, then the effect is reset

`delayBypass` / `reverbBypass` ramp the section's return gain to 0 over 20 ms
(`Routing::smoothingMs`). Once the fade has completed the effect's `reset()` is called and
it stops being processed. Un-bypassing therefore starts from an empty delay line / tank,
so a repeat or tail from *before* the bypass never reappears. This is the behaviour of a
hardware insert being switched out, and it keeps bypassed sections free on the CPU.

### Level knobs: −60 dB is "off"

`delayLevel` / `reverbLevel` at their minimum (≤ −59.9 dB) produce exactly 0, not
−60 dB, so "all the way down" silences the section instead of leaving a −60 dB residue.

### Gain smoothing

Trims, mix and section levels ramp linearly over 20 ms (`juce::LinearSmoothedValue`).
The equal-power mix smooths the cos/sin gains, not the mix percentage, so the ramp costs
no trig per sample; the deviation from a true equal-power path during the 20 ms ramp is
negligible.

## Alternatives considered

- Attack/release defined as one time constant (63 %) or 10 → 90 %: rejected, see above.
- Peak detector instead of 8 ms RMS: rejected by SPEC §3 (RMS is more musical on vocals);
  the cost is the ~20 ms detector lag noted above.
- Bypass that keeps the effect running silently (so un-bypass reveals the old tail):
  rejected; surprising when used live, and it wastes CPU on bypassed sections.
