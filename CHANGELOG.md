# Changelog

Internal revisions are `v0.N`; whole numbers are shipped builds (CLAUDE.md §5).
Each entry: what changed, what Seth should listen for, open questions.

## v0.2 — Phase 1: Routing + ducker (2026-09-13)

### Added
- `dsp/Ducker`: SPEC §3 sidechain ducker (Giannoulis 2012 log-domain feed-forward): key HPF
  (2nd-order Butterworth) → 8 ms RMS detector → 6 dB soft knee, 20:1, clamped to `Depth` →
  attack / hold / release ballistics → linear gain on the wet return. One instance per section,
  `duckLink` mirrors all seven delay values onto the reverb ducker, `duckSource = External` uses
  the sidechain bus when the host connects it and falls back to the input otherwise. Gain
  reduction published through atomics (`getDelayGainReductionDb()` / `getReverbGainReductionDb()`).
- `dsp/Routing`: SPEC §2 signal flow — input/output trim, per-section level, click-free bypass
  (20 ms crossfade), Serial/Parallel reverb feed (20 ms crossfade), equal-power mix, tail
  reporting. Every gain smoothed; `mix = 0` with trims at 0 dB is a sample-exact pass-through.
- `dsp/Biquad`, `dsp/Smoother`, `dsp/Util`: small pure-DSP building blocks.
- `dsp/standin/StandInDelay`, `StandInReverb`: **temporary** effects so the ducker can be heard
  and measured end to end. Naive by design (delay time changes click; the "reverb" is four
  combs). Replaced in Phases 2/3 and moved to `archive/`.
- Processor: real `processBlock` order (copy inputs → trim → key → delay → level → duck → bypass
  → reverb in → reverb → level → duck → bypass → equal-power mix → output trim), sub-block loop
  so a host block larger than `prepareToPlay` never overflows, outputs written last (mono-in
  layout: output ch 1 aliases sidechain ch 0). `getTailLengthSeconds()` is real. Zero latency.
- Render tool: `--sidechain <source|file.wav>`, `--sc-freq`, `--sc-level`, `--mono-in`; `--stats`
  now also prints `tailSeconds`, `usedExternalKey`, `duck.delayGRmaxDb`, `duck.reverbGRmaxDb`.
  Library: `RenderSettings::sidechain / monoInput / perBlockHook`, per-block GR in `RenderOutput`.
- Tests (35, all green): ducker depth / attack / hold / release / key HPF / re-trigger / unity /
  rate-and-block sweep; routing mix law, tail, trims, serial vs parallel, click-free bypass,
  external sidechain (stereo + mono bus, fallback), duck link/enable, mono-in aliasing guard,
  silence in → silence out, NaN/Inf sweep at parameter extremes, CPU budget.
- Docs: ADR-0002 (detector calibration, 95 % ballistics convention, hold semantics, serial
  feeds the ducked delay, bypass/mix/tail rules, stand-ins).

### Changed
- Default state is no longer a pass-through (mix 50 %, delay + comb reverb running). The
  Phase 0 round-trip tests and the CI render smoke now set `mix=0`.
- Project version 0.2.0.

### Verified in this session (Linux container)
- Release (GCC 13) and Debug (Clang 18) build clean with `-Werror`; `ctest` 35/35 in both.
- pluginval 1.0.4 `--strictness-level 10` on the Linux VST3: **SUCCESS**.
- CPU: 60 s stereo at 48 kHz / 512 renders at **0.5 % realtime** (budget 3 %; stand-ins are
  trivial, expect this to rise in Phases 2–5).
- **Not run here (needs macOS):** AU build, `auval`, pluginval on the AU. CI covers these.

### What Seth should listen for
Load the Standalone or VST3/AU on a vocal, `mix` ≈ 40 %, defaults otherwise. The stand-in
delay/reverb are ugly on purpose — listen **only** to the ducking:
1. Do the repeats/tail drop out of the way while you sing and swell back in the gaps?
2. Attack 5 ms / Hold 60 ms / Release 400 ms: too snappy, too slow, pumping? Try Release
   200 vs 800.
3. Threshold −30 dBFS: does it trigger on breaths/room noise (raise it) or miss quiet
   phrases (lower it)?
4. Key HPF 120 Hz: does a plosive or a low note still trigger the duck?
5. `duckSource = External` with a sidechain from another track in your DAW — confirm the
   plugin ducks from it and falls back to the input when nothing is routed.
Write dated notes in `docs/LISTENING_NOTES.md`.

### Open questions
- The ballistics convention (95 % in *T*) is a judgement call; if the knobs feel wrong,
  say whether it's the attack or the release and by roughly how much.
- The CLAUDE.md §6 "wet return peak ≤ input + 6 dB at max feedback" check belongs to the
  BBD/Tape soft-clip (Phase 5); a resonant digital delay at 95 % feedback legitimately
  exceeds it, so it is not enforced on the Phase 1 stand-in.

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
