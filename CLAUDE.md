# CLAUDE.md — project conventions for Claude Code

This repo is a JUCE audio plugin: a **ducking delay + reverb** whose effect returns are
sidechain-compressed by the dry input so they "get out of the way" of a vocal and swell
back in musically. Full product/DSP spec lives in `docs/SPEC.md`; the phased build plan
and acceptance criteria live in `docs/BUILD_PLAN.md`. **Read both before doing anything.**

The owner (Seth) is a live/broadcast audio engineer building this to sell commercially
under Richardson Media Solutions LLC. He can audition builds in a DAW between sessions;
you cannot listen, so verification must be measurable (see "Verification without ears").

---

## 0. Fill these in before the first session (Seth edits this block)

| Field | Value |
|---|---|
| Plugin name (display) | `Clear Space` |
| Plugin name (code-safe, no spaces) | `ClearSpace` |
| Company | Richardson Media Solutions |
| Manufacturer code (4 chars, ≥1 uppercase) | `Rmsl` |
| Plugin code (4 chars, ≥1 uppercase) | `Clsp` |
| Bundle ID | `com.richardsonmediasolutions.clearspace` |
| JUCE version | `8.0.15` (FetchContent, pinned; see `docs/decisions/ADR-0001-juce-fetch.md`) |
| Formats v1 | VST3, AU, Standalone (AAX later — do not add SDK now) |
| Target OS v1 | macOS (Windows build kept compiling but not tested until later) |
| License | Proprietary / All rights reserved (see §4 on third-party code) |

---

## 1. Toolchain (non-negotiable)

- **CMake + JUCE**, never Projucer. JUCE is pulled via `FetchContent` pinned to an exact
  git tag in `CMakeLists.txt` (or as a git submodule at `external/JUCE` — pick one in
  Phase 0 and document it in `docs/decisions/`).
- C++20. Compile with `-Wall -Wextra -Werror` on our own targets.
- Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
  Debug build: `cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug`
- Tests: Catch2 (via FetchContent). `ctest --test-dir build --output-on-failure`
- Validation: `pluginval --strictness-level 10 --validate <path-to-plugin>` and
  `auval -v aufx <PluginCode> <ManufacturerCode>` for AU. Both must pass before any tag.
- Formatting: `.clang-format` in repo root (LLVM base, 4-space indent, 100 col). Run it.
- Do not introduce dependencies beyond JUCE, Catch2, and (optionally) a small
  permissively-licensed WAV reader for the render tool. Ask before adding anything else.

## 2. Repo layout (create in Phase 0, keep to it)

```
CMakeLists.txt
CLAUDE.md
CHANGELOG.md
README.md
docs/
  SPEC.md
  BUILD_PLAN.md
  LISTENING_NOTES.md      # Seth's dated DAW listening feedback; read at session start
  decisions/              # ADR-NNNN-short-title.md, one per non-obvious design choice
src/
  PluginProcessor.{h,cpp}
  PluginEditor.{h,cpp}
  Parameters.{h,cpp}      # single source of truth for APVTS layout + IDs
  dsp/                    # pure DSP, no JUCE GUI includes, no allocation in process()
    Ducker.{h,cpp}
    DelayLine.h           # fractional delay line (header-only template)
    delay/  DigitalDelay, BBDDelay, TapeDelay, DelayEngine (mode switcher)
    reverb/ PlateReverb, HallReverb (FDN), RoomReverb, ReverbEngine
    Routing.{h,cpp}       # dry/wet split, serial/parallel, level, mix
  ui/
    LookAndFeel.{h,cpp}
    components/           # Knob, SegmentedControl, GRMeter, SectionPanel
tests/                    # Catch2 unit + offline-render tests
tools/
  render/                 # CLI: run the processor offline on WAV/synthetic input
presets/                  # factory presets as .xml (APVTS state)
```

## 3. Realtime-safety rules (hard)

- **No allocation, locks, logging, or file I/O in `processBlock`.** All buffers sized in
  `prepareToPlay`. Use `juce::ScopedNoDenormals`.
- Every user-facing parameter goes through `juce::AudioProcessorValueTreeState` and is
  smoothed (`juce::SmoothedValue` or one-pole) before touching audio. No zipper noise.
- Parameter IDs are declared once in `Parameters.h` as `constexpr` strings. UI and DSP
  both include it. Never type an ID string literal anywhere else.
- Meter data (gain reduction, levels) flows DSP → UI through `std::atomic<float>` only.
  The editor polls on a `juce::Timer` (30 Hz). Never call into the editor from the
  audio thread.
- Zero added latency by default. If a feature needs lookahead, it must be optional,
  off by default, and reported via `setLatencySamples`. This plugin is used live.
- All delay-time/size changes must be click-free (crossfade or slew per SPEC).
- Guard every feedback path against runaway: hard limit feedback coefficient and
  soft-clip inside the loop where the spec calls for it. Never allow NaN/Inf to
  propagate — the render tool checks for this and it is a test failure.

## 4. Third-party code and licenses

- Implement DSP **from the published papers/articles cited in SPEC.md**, in our own
  code. Reading open-source implementations for understanding is fine; **copying GPL
  code (e.g. ChowTapeModel, many reverb repos) is not** — this plugin is sold closed-source.
- Anything vendored must be MIT/BSD/Apache/0BSD/public-domain, recorded in
  `THIRD_PARTY_NOTICES.md` with its license text.
- When in doubt, stop and ask.

## 5. Version control (Seth's conventions — follow exactly)

- **Never delete files or history.** Superseded code moves to `archive/` or stays
  reachable via git tags/branches. Never `git push --force`, never delete branches.
- Internal working revisions increment by decimal: `v0.1`, `v0.2`, … `v0.9`, `v0.10`.
  Whole numbers (`v1`, `v2`) are reserved for **published builds shipped to others**.
- One git tag per completed phase (see BUILD_PLAN). Tag format `v0.N`.
- Commit early and often with conventional-style messages
  (`feat(dsp): …`, `fix(ui): …`, `test: …`, `docs: …`). Every commit builds.
- Every phase ends with a `CHANGELOG.md` entry under the new tag: what changed, what
  Seth should listen for, and any open questions.
- Non-obvious design choices get an ADR in `docs/decisions/` (what, why, alternatives).

## 6. Verification without ears

You cannot hear the output. Every DSP phase must ship with **measurable** checks in
`tests/` that run under `ctest`, using the offline render tool in `tools/render`:

- No NaN/Inf/denormal in output for all modes at 44.1k, 48k, 96k, block sizes 1–4096.
- Silence in → silence out (after tail) for every mode.
- Delay time accuracy: impulse in, measure first echo position, within ±1 sample
  (Digital) / documented tolerance (BBD/Tape).
- Reverb decay: measure RT60 via Schroeder backward integration on an impulse
  response; must be within ±15% of the `Decay` parameter across its range.
- Ducker: feed a −6 dBFS sine burst to the key, measure wet-return attenuation reaches
  `Depth` (±1 dB) within `Attack`, and recovers to −1 dB of unity within `Release` after
  hold. Verify sidechain HPF stops a 40 Hz key from triggering at threshold.
- Level sanity: wet return peak never exceeds input peak + 6 dB with feedback at max
  (BBD/Tape soft-clip working).
- CPU: render 60 s of stereo at 48k/512 and print realtime ratio; fail if a single mode
  costs > 3% of one core on the dev machine (log the number in CHANGELOG).

`tools/render` must support: input from WAV or synthetic (`impulse`, `sine`,
`burst`, `noise`, `speechlike` = AM-modulated pink noise at syllable rate ~4 Hz),
arbitrary parameter overrides via `--set id=value`, WAV output, and a `--stats` summary.

## 7. How sessions run

1. At session start: read `CLAUDE.md`, `docs/BUILD_PLAN.md` (find the current phase),
   `CHANGELOG.md` (last entry), and `docs/LISTENING_NOTES.md` (any new feedback).
2. Work **one phase per session** unless Seth says otherwise. Use plan mode first:
   propose concrete steps and files, ask questions, wait for a go.
3. Do the phase. Build, run tests, run pluginval. Fix until green.
4. Update `CHANGELOG.md`, add ADRs if needed, commit, tag `v0.N`.
5. End the session with a short "**What I need from you**" list for Seth: exactly what
   to build/open/listen for, and any decisions blocking the next phase.

## 8. Style of communication with Seth

Seth has ADHD; give him **specific action items**, not open-ended summaries.
When he asks "what do you need from me?", answer with a numbered list of concrete
things. Keep status updates short. Never draft emails on his behalf.
