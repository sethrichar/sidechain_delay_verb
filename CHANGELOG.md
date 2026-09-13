# Changelog

Internal revisions are `v0.N`; whole numbers are shipped builds (CLAUDE.md §5).
Each entry: what changed, what Seth should listen for, open questions.

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
