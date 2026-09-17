# Clear Space — ducking delay + reverb

A stereo delay and reverb plugin whose effect returns are sidechain-ducked by the dry
input, so they get out of the way of a vocal and swell back in the gaps. VST3, AU and
Standalone, macOS first. Built with CMake + JUCE 8.0.15.

- Product/DSP spec: [`docs/SPEC.md`](docs/SPEC.md)
- Phased plan and acceptance criteria: [`docs/BUILD_PLAN.md`](docs/BUILD_PLAN.md)
- Project conventions (for Claude Code sessions): [`CLAUDE.md`](CLAUDE.md)
- Design decisions: [`docs/decisions/`](docs/decisions/)

**Status:** Phase 1 (`v0.2`) — routing + ducker. The sidechain ducker, the signal flow
(serial/parallel, levels, bypass, equal-power mix, trims, tails) and the external sidechain
bus are real; the delay and reverb are temporary stand-ins (fixed 500 ms delay, comb
"reverb") until Phases 2–5. Generic editor with a live gain-reduction readout.

## Build

Requirements: CMake ≥ 3.25, a C++20 compiler, Ninja (optional). JUCE and Catch2 are
fetched automatically on first configure (network needed once).

macOS one-time setup:

```
xcode-select --install
brew install cmake ninja
brew install --cask pluginval
```

Linux (Ubuntu/Debian) one-time setup:

```
sudo apt-get install ninja-build libasound2-dev libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxext-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev
```

Then:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug -j
```

Outputs land in `build/ClearSpace_artefacts/<Config>/{VST3,AU,Standalone}/`. Pass
`-DCLEARSPACE_COPY_PLUGIN=ON` to have the plugins copied into your user plug-in folders
after each build (needed for `auval`).

Useful options: `-DCLEARSPACE_WERROR=OFF` (drop `-Werror`), `-DCLEARSPACE_BUILD_TESTS=OFF`,
`-DCLEARSPACE_BUILD_TOOLS=OFF`, `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` (use a local
JUCE checkout).

## Test

```
ctest --test-dir build --output-on-failure
```

Tests are Catch2 (`tests/`). DSP phases add offline-render tests that use the render tool
library, so acceptance criteria are measured, not listened for.

## Validate

```
pluginval --strictness-level 10 --validate "build/ClearSpace_artefacts/Release/VST3/Clear Space.vst3"
pluginval --strictness-level 10 --validate "build/ClearSpace_artefacts/Release/AU/Clear Space.component"
auval -v aufx Clsp Rmsl        # AU must be installed: build with -DCLEARSPACE_COPY_PLUGIN=ON
```

Both must pass before any `v0.N` tag. GitHub Actions (`.github/workflows/build.yml`) runs
the same steps on every push: macOS Release (universal) + Debug with pluginval and auval,
and Linux Release with pluginval under xvfb.

## Render tool

`tools/render` runs the processor offline on a WAV or a synthetic source. Binary path after
a build: `build/tools/render/render_artefacts/<Config>/render`.

```
render --in <impulse|sine|burst|noise|speechlike|path.wav> [--out out.wav]
       [--sr 48000] [--block 512] [--seconds 2.0] [--freq 1000] [--level -6]
       [--seed 1] [--set id=value]... [--set-at seconds:id=value]...
       [--sidechain <source|path.wav>] [--stats] [--tail]
```

- `--set` takes the parameter ID from `src/Parameters.h` and a value in natural units
  (`--set delayTime=375`), a choice name (`--set delayMode=Tape`) or `on`/`off`.
- `--stats` prints one `key=value` per line: `sampleRate`, `channels`, `samples`,
  `nanCount`, `infCount`, and per channel `chN.peak`, `chN.peakDb`, `chN.rms`, `chN.rmsDb`,
  `chN.nanCount`, `chN.infCount`, `chN.denormalCount`, `chN.firstNonZero`.
- `--set-at 1.0:delayBypass=on` changes a parameter mid-render (at the first block starting
  at or after that time). `--sidechain sine` (or a WAV) enables the sidechain bus and feeds
  it; use with `--set duckSource=External`.
- Output WAV is 32-bit float, so a dry-path round trip (`--set mix=0`) is bit-exact.
- Exit codes: `0` ok, `1` usage error, `2` render/IO error, `3` NaN/Inf in the output.

## Formatting

`.clang-format` (LLVM base, 4 spaces, 100 columns). Run before committing:

```
clang-format -i src/*.{h,cpp} src/dsp/*.{h,cpp} src/dsp/standin/*.h tests/*.{h,cpp} tools/render/*.{h,cpp}
```

## Layout

```
CMakeLists.txt           project, JUCE/Catch2 pins, plugin target, -Werror policy
src/                     PluginProcessor, PluginEditor, Parameters (APVTS single source of truth)
src/dsp/                 pure DSP: Ducker, Routing, Biquad, Effect interface; standin/ (Phase 1 stand-ins)
src/ui/                  custom look-and-feel and components (Phase 6)
tests/                   Catch2 tests, discovered into ctest
tools/render/            offline render library + CLI
docs/                    SPEC, BUILD_PLAN, LISTENING_NOTES, decisions/ (ADRs)
presets/                 factory presets (Phase 6)
```
