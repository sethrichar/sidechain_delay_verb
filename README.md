# Clear Space — ducking delay + reverb

A stereo delay and reverb plugin whose effect returns are sidechain-ducked by the dry
input, so they get out of the way of a vocal and swell back in the gaps. VST3, AU and
Standalone, macOS first. Built with CMake + JUCE 8.0.15.

- Product/DSP spec: [`docs/SPEC.md`](docs/SPEC.md)
- Phased plan and acceptance criteria: [`docs/BUILD_PLAN.md`](docs/BUILD_PLAN.md)
- Project conventions (for Claude Code sessions): [`CLAUDE.md`](CLAUDE.md)
- Design decisions: [`docs/decisions/`](docs/decisions/)

**Status:** Phase 1 (`v0.2`) — routing + ducker. The SPEC §2 signal flow and the SPEC §3
sidechain ducker are real and measured; the delay and reverb themselves are still throwaway
stand-ins (`src/dsp/standin/`) until Phases 2 and 3. Generic editor until Phase 6.

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
       [--seed 1] [--set id=value]... [--stats] [--tail]
       [--sidechain <source|path.wav>] [--sc-freq 1000] [--sc-level -6] [--mono-in]
```

- `--set` takes the parameter ID from `src/Parameters.h` and a value in natural units
  (`--set delayTime=375`), a choice name (`--set delayMode=Tape`) or `on`/`off`.
- `--stats` prints one `key=value` per line: `sampleRate`, `channels`, `samples`,
  `nanCount`, `infCount`, and per channel `chN.peak`, `chN.peakDb`, `chN.rms`, `chN.rmsDb`,
  `chN.nanCount`, `chN.infCount`, `chN.denormalCount`, `chN.firstNonZero`.
- `--sidechain` feeds the sidechain bus (mono or stereo WAV, or a synthetic source sharing
  `--seconds`/`--seed` with `--in`); pair it with `--set duckSource=External`. `--mono-in` uses
  the mono→stereo bus layout. `--stats` then also reports `tailSeconds`, `usedExternalKey`,
  `duck.delayGRmaxDb` and `duck.reverbGRmaxDb`.
- Output WAV is 32-bit float, so a round trip with `--set mix=0` is bit-exact (the default
  mix is 50 %, so the raw default is no longer a pass-through).

Quick ducking check:

```
render --in sine --sidechain burst --set duckSource=External --set mix=100 --stats | grep duck
```
- Exit codes: `0` ok, `1` usage error, `2` render/IO error, `3` NaN/Inf in the output.

## Formatting

`.clang-format` (LLVM base, 4 spaces, 100 columns). Run before committing:

```
clang-format -i src/*.{h,cpp} src/dsp/*.{h,cpp} src/dsp/standin/*.h tests/*.cpp tools/render/*.{h,cpp}
```

## Layout

```
CMakeLists.txt           project, JUCE/Catch2 pins, plugin target, -Werror policy
src/                     PluginProcessor, PluginEditor, Parameters (APVTS single source of truth)
src/dsp/                 pure DSP: Ducker, Routing, Biquad, Smoother; standin/ = Phase 1 placeholders
src/ui/                  custom look-and-feel and components (Phase 6)
tests/                   Catch2 tests, discovered into ctest
tools/render/            offline render library + CLI
docs/                    SPEC, BUILD_PLAN, LISTENING_NOTES, decisions/ (ADRs)
presets/                 factory presets (Phase 6)
```
