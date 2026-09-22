# archive/

Superseded code that is kept reachable rather than deleted (CLAUDE.md §5). Nothing in here is
compiled.

- `standin/StandInDelay.h` — Phase 1 stand-in delay (fixed 500 ms, integer samples, feedback
  only). Replaced in Phase 2 by `src/dsp/delay/DigitalDelay` behind `DelayEngine`.
- `standin/StandInReverb.h` — Phase 1 stand-in reverb (four parallel feedback combs per channel,
  loop gains set from the decay time). Replaced in Phase 3 by `src/dsp/reverb/PlateReverb`
  behind `ReverbEngine`.
