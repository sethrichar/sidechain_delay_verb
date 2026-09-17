# Changelog

Internal revisions are `v0.N`; whole numbers are shipped builds (CLAUDE.md §5).
Each entry: what changed, what Seth should listen for, open questions.

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

### Open questions
1. Hold is effectively `hold + ~20 ms` with a hot key (ADR-0003). Fine, or compensate?
2. Ducker disabled fades over 20 ms rather than the release time (ADR-0003). OK?
3. Serial tail reports delay + reverb (sum) instead of SPEC's max (ADR-0002). OK?

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
