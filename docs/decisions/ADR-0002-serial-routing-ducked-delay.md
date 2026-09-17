# ADR-0002 — In Serial routing the reverb receives the *ducked* delay return

**Status:** accepted (Phase 1, 2026-09-17)

## Decision

With `routing = Serial`, the reverb input is `in + delayWet`, where `delayWet` is the delay
return **after** its level, bypass fade and ducker gain have been applied:

```
delayWet  = duckD · levelD · bypassD · Delay(in)
reverbIn  = Serial ? in + delayWet : in
reverbWet = duckR · levelR · bypassR · Reverb(reverbIn)
out       = outTrim · (dry · cos(mix·π/2) + (delayWet + reverbWet) · sin(mix·π/2))
```

The dry input always feeds the reverb directly as well, so Serial is "delay into reverb"
in the sense of a send chain, not a pure cascade. (`src/dsp/Routing.cpp`)

Consequences that follow from the same decision:

- **Tail in Serial is the sum** of the delay tail and the reverb decay, because the reverb
  keeps ringing after the last audible repeat. Parallel reports the longer of the two.
  SPEC §2 says "max"; the sum is the correct worst case for offline bounces and only ever
  reports *more* silence, never less.
- The reverb ducker still acts on the reverb return independently, so with `duckLink`
  off the two returns can duck differently even in Serial.

## Why

- **It is what the manual technique does.** On a console the compressor sits on the delay
  *return*; if that return is sent on to a reverb, the reverb hears the ducked repeats.
  Users expect the plugin to behave like the routing they already know.
- **Musically safer.** Feeding the reverb the un-ducked delay would let the delay's
  repeats bloom in the reverb right under the vocal, undoing the ducking. With the ducked
  return the reverb's contribution from the repeats also "gets out of the way".
- **One gain stage, one meter.** The delay GR meter shows exactly what the reverb is
  receiving; there is no hidden second copy of the delay signal.

## Alternatives considered

- **Reverb fed from the un-ducked delay output.** Rejected: defeats the ducking on the
  reverb side and needs a second, un-metered path.
- **Pure cascade (reverb receives only the delay, not the dry input).** Rejected: makes
  Serial useless with the delay bypassed and changes the reverb's onset character
  depending on delay settings. Users who want that can set `delayFeedback` and levels to
  taste; the dry feed stays.
- **Duck inside the feedback loop.** Out of scope for v1 (SPEC §2).
