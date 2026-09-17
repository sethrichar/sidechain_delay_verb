# Changelog

Internal revisions are `v0.N`; whole numbers are shipped builds (CLAUDE.md §5).
Each entry: what changed, what Seth should listen for, open questions.

## v0.3 — Phase 2: Digital delay (2026-09-17)

### Added
- `src/dsp/DelayLine.h`: header-only fractional delay line (power-of-two ring, 4-point
  3rd-order Hermite read, integer read, NaN-guarded clamp). Reads are relative to the next
  write so feedback can be folded into the written sample.
- `src/dsp/delay/DigitalDelay`: SPEC §4.1. 1–2000 ms; **two read heads on one buffer with a
  30 ms equal-power crossfade on every time change** (no pitch artefact; a change during a
  crossfade waits for it); feedback 0–100 % → loop coefficient 0–0.98 (hard clamp, no
  saturation); `delayLowCut` / `delayHighCut` 2nd-order filters **in the feedback path only**
  (first echo untouched, repeats shaped); sine modulation of the read position up to 2 ms
  (`delayMod`, `delayModRate`), right channel in quadrature; **Stereo** (independent L/R) or
  **PingPong** (mono sum → left line, cross-fed returns, echoes alternate L R L …) with a 20 ms
  blend between the two; 20 ms smoothing on feedback, mod depth and cutoffs. Near-integer
  delays are snapped so a 500 ms echo at 96 kHz is bit-exact. Details in ADR-0004.
- `src/dsp/delay/DelayEngine`: the `Effect` the processor owns for the delay section — mode
  selection, SPEC §6 per-mode time clamps (Digital 1–2000, BBD 20–1000, Tape 20–2000 ms),
  `noteLengthSeconds(bpm, note, modifier)` for tempo sync, tail. **BBD and Tape run the Digital
  delay until Phase 5.**
- Processor: `delaySync` / `delayNote` / `delayNoteMod` resolve against the host playhead's
  BPM (read every block, playing or stopped; **120 BPM fallback** when there is no playhead or
  tempo, e.g. Standalone). `getEffectiveDelayTimeMs()` and `getHostBpm()` atomics for the
  tail and the Phase 6 UI. All delay parameters are now live.
- Render tool: `--bpm <n>` gives the processor a playing transport at that tempo (mock
  `AudioPlayHead`); library `RenderSettings::bpm`.
- Tests (47, all green on Release/GCC and Debug/Clang), new in `tests/test_delay.cpp`:
  DelayLine (integer exactness, Hermite reproduces a quadratic, fractional sine < −60 dB
  error, clamped/NaN reads safe); first echo within ±1 sample at 44.1/48/96 k for 1, 23.7,
  375, 1000, 2000 ms (arrival = amplitude centroid, sums to unity); repeats at −6 dB per pass
  at 50 % (±0.3 dB) and bounded at 100 % (≤ 1/(1−0.98) on a steady tone, ≥ 55 dB down after
  the reported tail); low/high cut drop a 100 Hz / 10 kHz repeat by ≥ 30 dB while the first
  echo is unchanged; defaults pass 1 kHz within 0.5 dB per repeat; **time jump 100 → 1000 ms
  and a 75-step sweep at 0 % and 50 % feedback with max sample step < signal's own + 0.1**
  (and a hand-spliced jump proves the measure would catch a click); crossfade length
  30 ± 1.5 ms; modulation pitch deviation 2π·rate·depth (2.5 % at 2 Hz / 100 %, ±15 %; half
  at 50 %; < 0.1 % at 0 %; L/R differ); ping-pong alternates L R L with the other channel
  < −80 dB, Stereo keeps L = R, a left-only input stays left, mode switch mid-stream is
  smooth; note-length table and per-mode clamps; sync follows a mocked playhead at 120, 90
  and 140 BPM for straight/dotted/triplet notes (±1 sample), sync off ignores the playhead,
  no playhead → 120 BPM, a whole note at 60 BPM clamps to 2 s; silence in → exactly silence
  out and no NaN/Inf over 3 rates × 5 block sizes × {1 ms, 2000 ms} × {Stereo, PingPong} at
  100 % feedback / 100 % mod at 10 Hz; CPU ratio.
- ADR-0004 (crossfade design, 0.98 feedback ceiling, filters in the loop, quadrature LFO,
  ping-pong topology, sync fallback, tail formula, BBD/Tape stub).
- `archive/standin/StandInDelay.h`: the Phase 1 stand-in delay, moved out of `src/` (kept per
  CLAUDE.md §5; `archive/README.md` indexes it).

### Changed
- Routing/smoke tests pin `delayTime = 500` and `delayMod = 0` where timing matters (the
  stand-in was fixed at 500 ms; defaults are now 375 ms with 10 % modulation). Default tail is
  now 5.016 s (0.377 s × 8 repeats + 2 s reverb, Serial); the delay tail includes the 2 ms
  modulation allowance.
- CI Linux render smoke also checks a synced echo at 100 BPM lands on sample 14400.
- Project version 0.3.0.

### Measured (Linux container, Release/GCC 13, 48 kHz)
- CPU, whole chain at defaults, 48 kHz / 512: **0.58 % of one core** (20 s in 0.116 s).
  Delay + ducker alone with 50 % mod and ping-pong: **0.51 %**. Debug/Clang not enforced.
- pluginval 1.0.4 `--strictness-level 10` on the Linux VST3: **SUCCESS**.
- **Not run here (needs macOS):** AU build, `auval`, pluginval on the AU — CI covers these.

### What Seth should listen for
The delay is now real (Digital mode). Judge it plus the ducker; ignore the reverb (still the
metallic stand-in) and treat BBD/Tape as "Digital with different time limits" for now.
1. **Time-change feel:** sweep Delay Time while a vocal or loop is running. You should hear
   clean crossfades with no pitch swoop and no clicks. Does 30 ms feel right, or does a fast
   sweep sound "steppy"? (ADR-0004: a 50–80 ms crossfade is a one-line change.)
2. **Feedback at 100 %:** hold a phrase into it. It should build toward roughly +34 dB on a
   sustained tone and never run away; repeats eventually fade. Is the 0.98 ceiling right, or
   do you want true infinite hold (would need a limiter in the loop — decision below)?
3. **Low/High Cut** at the 150 Hz / 8 kHz defaults: do repeats sit under a vocal the way you
   want, or should the defaults be wider/narrower?
4. **Mod** at the 10 % / 0.8 Hz defaults: subtle chorus on the repeats. Too much, too little,
   or should default be 0?
5. **Ping-Pong** on a mono vocal: first repeat left, then right. Confirm the switch from
   Stereo to Ping-Pong mid-song has no click.
6. **Sync** in your DAW at a few tempos, 1/8 dotted and 1/4 triplet: the repeats should sit
   on the grid. Also check the Standalone (no tempo → 120 BPM).
7. **1 ms** delay with high feedback and mod: it's a comb/flanger; harmless, just confirm no
   nasties.

### Open questions for Seth
1. Feedback ceiling 0.98 vs. true infinite (1.0 with an in-loop limiter)? Default is 0.98.
2. Crossfade length 30 ms (SPEC's "≈30 ms") — keep, or longer for smoother knob sweeps?
3. L/R modulation in quadrature (stereo movement) vs. identical (mono-compatible repeats)?
4. Listening checkpoint #1 is due after Phase 3 (Plate); the v0.2 listening pass was deferred
   to this build, so both the ducker notes and the delay notes can go in one LISTENING_NOTES
   entry.

## v0.2 — Phase 1: Routing + ducker (2026-09-17)

### Added
- `src/dsp/Ducker`: SPEC §3 sidechain ducker (Giannoulis et al. 2012 log-domain feed-forward
  design). 2nd-order key HPF → 8 ms RMS detector → soft-knee (6 dB) 20:1 gain computer
  clamped to **Depth** → attack / **hold** / release ballistics in dB → linear gain on the
  return. Block-peak gain reduction published for the meter. Timing definitions and hold
  semantics in ADR-0003.
- `src/dsp/Routing`: SPEC §2 signal flow — dry/wet split, Serial/Parallel, per-section level
  (−60 dB = off) and 20 ms click-free bypass (effect is reset once faded out), ducker gains
  on the returns, equal-power mix, output trim, tail reporting (Serial = delay + reverb
  tails, see ADR-0002).
- `src/dsp/Biquad.h` (RBJ HPF/LPF, TDF-II) and `src/dsp/Effect.h` (engine interface with an
  atomic tail length) — reused by the delay filters and the real engines from Phase 2.
- Stand-in effects (`src/dsp/standin/`, to be moved to `archive/` when replaced): a plain
  500 ms delay with feedback (`delayFeedback`, clamped to 98 %) and a 4-comb-per-channel
  "reverb" whose loop gains are calibrated so RT60 = `reverbDecay`.
- Processor: input trim → key (`duckSource` Internal = post-trim input, External = sidechain
  bus, **falls back to internal when the bus is not connected**) → two duckers (`duckLink`
  makes the reverb ducker mirror all seven delay-duck parameters) → routing. Mono→stereo and
  stereo→stereo, mono or stereo sidechain. Oversized host blocks are chunked, never overrun.
  Meter feeds: `getDelayGainReductionDb()`, `getReverbGainReductionDb()`,
  `isUsingExternalKey()` (atomics, block peak).
- Generic editor now shows a live "Duck GR" readout (30 Hz timer) so the ducker can be seen
  working in a DAW before the Phase 6 UI.
- Render tool: `--sidechain <source|file.wav>` enables the sidechain bus and feeds it;
  `--set-at seconds:id=value` applies a parameter change mid-render (block-quantised).
  Library: `RenderSettings::sidechain`, `RenderSettings::automation`.
- Tests (35, all green on Release/GCC and Debug/Clang): ducker gain computer, depth (±0.15 dB
  at 12 / 24 / 3 dB), attack, hold, release, re-trigger, key HPF (40 Hz key ignored, 1 kHz
  triggers), disable fade, NaN safety, sample-rate and block-size independence; Biquad
  response; equal-power mix law; trims; stand-in echo at 500 ms ±1 sample at 44.1/48/96 k;
  stand-in RT60 within ±15 % at 0.5/2/5 s; Serial vs Parallel; click-free bypass and
  smoothing (max sample step ≤ the signal's own); tail reporting; silence in → exactly
  silence out; ≥ 55 dB decay after the reported tail; end-to-end ducking of both returns
  (−12 dB ±1) and release swell; `duckLink`; external sidechain ducks the wet while the main
  input is silent, Internal ignores the bus, External-with-no-bus falls back; mono-in +
  sidechain aliasing safety; NaN/Inf sweep over 3 rates × 5 block sizes × 2 routings at
  100 % feedback; CPU ratio.
- ADR-0002 (Serial feeds the reverb with the ducked delay return), ADR-0003 (ducker timing
  definitions, hold semantics, disable fade, bypass reset, level-off, smoothing).

### Changed
- Phase 0 round-trip tests now pin `mix = 0`; the dry path stays bit-exact (the effects run
  but are multiplied by an exact 0). CI's render smoke step does the same and also renders
  with an external sidechain.
- `getTailLengthSeconds()` is no longer 0: defaults report 6 s (delay 35 % feedback → 4 s,
  plus 2 s reverb, Serial).

### Measured (Linux container, Release/GCC 13, 48 kHz)
- Ducker at defaults with a −6 dBFS 1 kHz key: attack 4.25 ms (5 % → depth−1 dB),
  hold 81 ms (60 ms + ~20 ms detector fall), release 326 ms (release start → 1 dB).
- CPU, whole chain at defaults, 48 kHz / 512: **0.64 % of one core** (20 s rendered in
  0.128 s). Debug/Clang: ~2.5 % (not enforced).
- pluginval 1.0.4 `--strictness-level 10` on the Linux VST3: **SUCCESS**.
- **Not run here (needs macOS):** AU build, `auval`, pluginval on the AU — CI covers these.

### What Seth should listen for
Everything is a stand-in except the ducker and the routing, so judge only those:
1. **Ducking feel** on a vocal with the delay at 35–50 % feedback and the reverb at 2 s:
   is 12 dB depth / 5 ms / 60 ms / 400 ms the right default? Does the swell after a phrase
   feel musical or too fast/slow? (Watch the "Duck GR" line in the generic editor.)
2. **Threshold** −30 dBFS with your typical vocal level: does it duck on every syllable or
   only on the loud ones? Adjust and note what number felt right.
3. **Key HPF** 120 Hz: any pumping from plosives or breath?
4. **Serial vs Parallel** with delay into reverb: does Serial sound like "delay into verb"?
5. **Bypass/mix/level moves** while audio is running: any click.
6. **External sidechain** in your DAW (send a different track to the sidechain input with
   Duck Source = External): the readout should say "key: external".
Ignore: the delay is fixed at 500 ms with no filters; the reverb is metallic by design.

### Decisions (Seth, 2026-09-17)
1. Hold is effectively `hold + ~20 ms` with a hot key (ADR-0003): **OK as is.**
2. Ducker disabled fades over 20 ms rather than the release time (ADR-0003): **OK.**
3. Serial tail reports delay + reverb (sum) instead of SPEC's max (ADR-0002): **OK.**
4. Listening pass on v0.2 deferred; do it before or during Phase 2 (LISTENING_NOTES).

## v0.1 — Phase 0: Scaffold (2026-09-13)

### Added
- CMake project, C++20, JUCE **8.0.15** via FetchContent (ADR-0001), Catch2 v3.16.0.
  Targets: VST3, AU, Standalone. `-Wall -Wextra` everywhere, `-Werror` on our own sources
  only (JUCE module translation units are exempt).
- `ClearSpaceProcessor`: stereo pass-through with the full APVTS layout (48 parameters,
  SPEC §6) and a "Sidechain" input bus (stereo, inactive by default). Accepts mono→stereo
  and stereo→stereo main layouts; sidechain off/mono/stereo. State carries
  `stateVersion = 1` for the Phase 7 migration hook.
- `Parameters.h/.cpp`: single source of truth for IDs (`ParamID::…`, plus `ParamID::all`),
  choice lists, log ranges (geometric-mean midpoint), unit-aware value text.
  `juce::ParameterID` version hint 1 on every parameter.
- Generic editor (`GenericAudioProcessorEditor`) so every parameter can be auditioned in a
  DAW or the Standalone before the real UI (Phase 6).
- `tools/render`: offline renderer (library + CLI). Sources: impulse, sine, burst, noise,
  speechlike (AM pink noise at 4 Hz). WAV in/out (32-bit float), `--set id=value`, `--stats`
  (peak, RMS, NaN/Inf/denormal counts, first-nonzero index), `--tail`.
- Tests (13, all green): smoke; SPEC §6 layout guard (IDs, ranges, defaults, choice order,
  state round trip); render round trip bit-exact at 44.1/48/96 kHz, block sizes 1–4096;
  stats and source checks; `--set` parsing and rejection.
- CI: `.github/workflows/build.yml` — macOS Release (universal) + Debug with ctest,
  pluginval strictness 10 (VST3 + AU) and auval; Linux Release with ctest and pluginval.
- Docs: README, THIRD_PARTY_NOTICES, ADR-0001, LISTENING_NOTES template.

### Verified in this session (Linux container)
- Release (GCC 13) and Debug (Clang 18) build clean with `-Werror`; `ctest` 13/13.
- pluginval 1.0.4 `--strictness-level 10` on the Linux VST3: **SUCCESS**.
- Render CLI WAV round trip: output file byte-identical to input.
- **Not run here (needs macOS):** AU build, `auval`, pluginval on the AU. CI covers these
  on push; Seth should confirm once on his Mac (see README → Validate).

### What Seth should listen for
Nothing — the plugin is a pass-through. Open the Standalone once to confirm it launches and
the generic parameter panel shows all sections (Global, Delay, Delay Duck, Reverb, Reverb Duck).

### Decisions (Seth, 2026-09-13)
1. **JUCE licence:** free tier for now. Revisit before v1 ships.
2. **Bus layouts:** mono→stereo for mono sources and stereo→stereo for stereo sources —
   both stay accepted (as implemented).
3. **CI:** macOS + Linux workflow runs on every push.
4. `main` and tag `v0.1` were created by Seth from this branch.
