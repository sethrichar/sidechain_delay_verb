# archive/

Superseded code that is kept reachable rather than deleted (CLAUDE.md §5). Nothing in here is
compiled.

- `standin/StandInDelay.h` — Phase 1 stand-in delay (fixed 500 ms, integer samples, feedback
  only). Replaced in Phase 2 by `src/dsp/delay/DigitalDelay` behind `DelayEngine`.
