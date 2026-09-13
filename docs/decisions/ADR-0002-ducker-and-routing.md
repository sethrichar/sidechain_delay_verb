# ADR-0002 — Ducker conventions and routing choices (Phase 1)

**Status:** accepted (Phase 1, 2026-09-13)

SPEC §2/§3 leave a few things to be decided at implementation time. These are the choices
made in Phase 1, why, and what was rejected. They are the contract the Phase 1 tests measure.

## 1. Detector calibration: RMS reads a sine's *peak* level

The ducker's detector is a one-pole average of the squared key (τ = 8 ms), scaled ×2 before
the log so that a steady sine reports its **peak** dBFS rather than its RMS (3 dB lower).
`duckThreshold` therefore lines up with what Seth sees on a console peak meter for a tone,
and the SPEC defaults (−30 dBFS threshold, −6 dBFS test key) mean what they say.

*Rejected:* true RMS (sine reads −3 dB) — correct in a textbook sense, but the threshold
would read 3 dB "hotter" than every meter Seth uses.

## 2. Attack / Release = time to complete 95 % of the move

Both ballistics are one-pole exponentials on the gain-reduction signal in dB (Giannoulis et
al. 2012, feed-forward log-domain topology). The parameter value *T* is defined as the time
in which 95 % of the move is done, i.e. the one-pole time constant is τ = T/3. This is what
makes CLAUDE.md §6's acceptance tests measurable as written: "reaches Depth ±1 dB within
Attack" and "recovers to −1 dB of unity within Release".

*Rejected:* τ = T (63 %, the raw one-pole convention) — the knob would feel ~3× slower than
labelled. 10–90 % conventions (τ ≈ T/2.2) — fine too, but 95 % keeps the ±1 dB tests honest
at the default 12 dB depth.

## 3. Hold semantics

While the gain computer asks for **at least** the current reduction (key above threshold,
or steady at depth) the hold timer is re-armed and the attack one-pole runs. The moment it
asks for less, the current reduction is **frozen** for `duckHold` ms, then released.

A re-trigger during release re-enters attack from wherever the gain currently is — no reset,
no jump. Note the 8 ms RMS detector adds its own decay after the key stops (≈ 20 ms before
the requested reduction drops below the clamped depth), so measured recovery is
`hold + release + ~20–30 ms` from key-off; the tests allow a 30 ms detector margin.

## 4. Serial routing feeds the reverb with the **ducked** delay return

`reverbIn = dry + delayWet` where `delayWet` is post level, post ducker, post bypass fade.
So when the vocal ducks the echoes, the reverb never blooms from echoes the user just pushed
down; the reverb's own ducker then handles the reverb of the dry signal. This is the
"get out of the way" behaviour the product is named for. The serial/parallel switch is a
20 ms crossfade of the delay-return contribution, so flipping it is click-free.

*Rejected:* feeding the reverb the un-ducked delay (reverb swells from ducked echoes,
smearing the very gap the ducker opened) — sonically contradicts the product goal.

## 5. Bypass keeps the effect running

Section bypass is a 20 ms linear crossfade of that section's return gain to zero; the delay
and reverb keep processing underneath. Un-bypassing therefore never exposes a stale buffer
or a truncated tail, at the cost of the (negligible) CPU of a bypassed effect.

## 6. Mix is equal-power; `mix = 0` is a bit-exact pass-through

`dry = cos(m·π/2)`, `wet = sin(m·π/2)`, each smoothed separately over 20 ms; the ends are
pinned to exactly 1/0 so that with mix 0 and the trims at 0 dB the output is sample-identical
to the input (the render tool's round-trip test relies on this).

## 7. Tail reporting

`getTailLengthSeconds = max(delay tail, reverb decay)`, where the delay tail is
`delayTime × (1 + number of echoes still above −60 dB)` with feedback clamped to 99 % for
the count, the reverb tail is `reverbDecay`, a bypassed section contributes 0, and the
result is capped at 30 s. Computed from parameter atomics only, so the call is const and
safe from any thread.

## 8. Stand-in effects (Phase 1 only)

`src/dsp/standin/StandInDelay.h` (integer delay, feedback clamped to 0.95, *not* click-free
on time change) and `StandInReverb.h` (four damped parallel combs, RT60 from `reverbDecay`)
exist only so the routing, ducker and tail logic can be tested end to end. They follow
`delayTime` / `delayFeedback` / `reverbDecay` so Seth can audition ducking at musical
times. They move to `archive/` when Phases 2 and 3 replace them — never deleted.
