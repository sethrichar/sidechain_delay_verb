# BUILD_PLAN.md — phases, acceptance criteria, session workflow

Work one phase per Claude Code session. Each phase ends green (build + tests +
pluginval), with a CHANGELOG entry, a commit, and a tag `v0.N`. Current phase is the
first one whose checkbox is unticked. Tick boxes as you complete them.

---

## Phase 0 — Scaffold  →  tag `v0.1`
- [x] CMake project with JUCE pinned; targets: VST3, AU, Standalone.
- [x] Empty stereo pass-through processor with a sidechain input bus declared
      (`withInput("Sidechain", stereo, false)`).
- [x] `Parameters.h/.cpp` with the **full** APVTS layout from SPEC §6 (all params exist
      from day one, even before they do anything — this keeps presets/automation stable).
- [x] Catch2 wired into `ctest` with one trivial test.
- [x] `tools/render` CLI: synthetic sources, WAV in/out, `--set`, `--stats` (peak, RMS,
      NaN/Inf count, first-nonzero-sample index).
- [x] `.clang-format`, `README.md` (how to build/test/validate), `CHANGELOG.md`,
      `docs/LISTENING_NOTES.md` (empty template), `docs/decisions/ADR-0001-juce-fetch.md`.
- [x] Optional: GitHub Actions macOS workflow that builds and runs `ctest`.
- **Accept:** builds Release + Debug with `-Werror`; `pluginval --strictness-level 10`
  and `auval` pass on the pass-through; render tool round-trips a WAV bit-exactly.

## Phase 1 — Routing + ducker  →  tag `v0.2`
- [x] `Routing`: dry/wet split, serial/parallel, per-section level, equal-power mix,
      click-free bypass, input/output trim, tail reporting.
- [x] `Ducker` per SPEC §3, two instances, `duckLink`, `duckSource` with sidechain-bus
      fallback. GR published via atomics.
- [x] Temporary stand-in effects: a plain 500 ms delay and a plain 2 s comb "reverb"
      so the ducker can be tested end to end. (Keep them in `archive/` later — don't
      delete.) — implemented in `src/dsp/standin/`, following `delayTime`/`reverbDecay`.
- **Accept:** ducker tests from CLAUDE.md §6 pass (depth, attack, hold, release, key
  HPF); external-sidechain test: key on bus 2 ducks wet while bus 1 is silent.

## Phase 2 — Digital delay  →  tag `v0.3`
- [ ] `DelayLine.h` fractional Hermite line; `DigitalDelay` with dual-line crossfade
      on time change, feedback filters, mod, stereo/ping-pong, tempo sync.
- **Accept:** echo position ±1 sample at 44.1/48/96k; time sweep 100→1000 ms renders
  with no click (max sample-to-sample jump < 0.1 above the signal's own); ping-pong
  alternates channels on successive echoes; sync follows a mocked playhead at 120 BPM.

## Phase 3 — Plate reverb (Dattorro)  →  tag `v0.4`
- [ ] `PlateReverb` per SPEC §5.1, calibrated so `reverbDecay` ≈ measured RT60.
- [ ] `ReverbEngine` interface + pre-delay, low/high cut, width.
- **Accept:** RT60 within ±15% across 0.3–10 s; impulse response has no isolated
  repeating peaks > −20 dB after 100 ms (metallic-ringing check via autocorrelation);
  stereo L/R correlation at 100% width < 0.5.

**Listening checkpoint #1** — Seth auditions Digital delay + Plate + ducker on a vocal.
Notes go in `docs/LISTENING_NOTES.md`. Address before Phase 4.

## Phase 4 — Hall + Room  →  tag `v0.5`
- [ ] `HallReverb` FDN per SPEC §5.2; `RoomReverb` per §5.3 (design 1).
- **Accept:** same RT60/ringing/correlation checks; Hall with modulation off shows
  measurable comb peaks that disappear with modulation on (proves the mod works).

## Phase 5 — BBD + Tape  →  tag `v0.6`
- [ ] `BBDDelay` per SPEC §4.2; `TapeDelay` per §4.3; `DelayEngine` mode switch is
      click-free (crossfade 50 ms).
- **Accept:** BBD bandwidth measured at 50 ms vs 800 ms differs by > 1.5 octaves;
  feedback at 100% stays bounded (peak < +6 dB over input) for 30 s in both modes;
  tape wow/flutter produces measurable pitch deviation on a 1 kHz sine at the
  expected rates; silence in → silence out (hiss gate works).

**Listening checkpoint #2** — all six modes on a vocal and on a snare/percussive source.

## Phase 6 — UI  →  tag `v0.7`
- [ ] `LookAndFeel`, `Knob`, `SegmentedControl`, `GRMeter`, `SectionPanel`, layout
      per SPEC §7, parameter attachments, mode-dependent visibility, resizing, preset
      bar, factory presets.
- **Accept:** every parameter reachable from the UI and bound correctly (automated
  test walks all attachments); `paint` allocation-free (assert with a debug allocator
  hook); UI opens/closes 100× in pluginval without leaks; screenshot exported to
  `docs/screenshots/` for Seth to review.

**Listening/looking checkpoint #3** — UI review against the Valhalla/Cradle/Ableton bar.

## Phase 7 — Polish + release candidate  →  tag `v0.8`, then `v1` when shipped
- [ ] State versioning in APVTS (store a `stateVersion` property; migration hook).
- [ ] CPU profile all modes; optimise hotspots; record numbers in CHANGELOG.
- [ ] Denormal/NaN fuzz test: random parameter automation for 5 min renders clean.
- [ ] Windows build compiles in CI (no Windows testing yet).
- [ ] Code signing / notarization script stub for macOS (Seth supplies credentials).
- [ ] Manual: one-page `docs/USER_GUIDE.md`.
- **Accept:** pluginval strictness 10 + auval clean on RC build; Seth signs off in
  LISTENING_NOTES; tag `v1`.

---

## Session workflow for Seth

**One-time repo setup**
1. Create the empty GitHub repo, clone it.
2. Copy `CLAUDE.md` to the root and `SPEC.md` + `BUILD_PLAN.md` into `docs/`.
3. Edit the table at the top of `CLAUDE.md` (name, codes, bundle ID, JUCE tag).
4. Confirm `pluginval` and Xcode command-line tools are installed
   (`brew install --cask pluginval` or download from Tracktion; `xcode-select --install`).
5. Commit: `git add -A && git commit -m "docs: project spec and build plan"`.

**Kickoff prompt (paste as the first message in Claude Code, in plan mode — press
Shift+Tab until it says "plan")**

```
Read CLAUDE.md, docs/SPEC.md, and docs/BUILD_PLAN.md in full before responding.
We are starting Phase 0. In plan mode, give me:
1. The exact file list you will create and the JUCE tag you propose to pin.
2. Any questions or ambiguities in the spec that would change how you build Phase 0
   (batch them — don't ask one at a time).
3. What you need from me before you start.
Do not write code until I say go.
```

**Every later session**
```
Read CLAUDE.md, docs/BUILD_PLAN.md, the last CHANGELOG.md entry, and
docs/LISTENING_NOTES.md. Continue with the current phase. Plan mode first: list the
steps, then wait for my go.
```

**At the end of every session, ask it:** "What do you need from me?" — it should
answer with a numbered list (things to listen for, decisions, credentials).

**Between sessions:** open the Standalone or load the VST3/AU in your DAW, listen,
write dated notes in `docs/LISTENING_NOTES.md`. Be concrete
("plate at 2 s decay feels metallic on sustained notes", "release swell too fast at
400 ms — try 600 default").

**Tips that keep Claude Code efficient**
- Run `/clear` between phases so context stays focused; the docs carry the state.
- If it starts a big refactor you didn't ask for, stop it and ask for an ADR first.
- If a build error loops more than twice, ask it to add a failing test that reproduces
  the problem before fixing.
- Never let it delete: if it wants to remove something, it moves it to `archive/`.
