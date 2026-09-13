# ADR-0001 — Pull JUCE via CMake FetchContent, pinned to tag `8.0.15`

**Status:** accepted (Phase 0, 2026-09-13)

## Decision

`CMakeLists.txt` fetches JUCE with `FetchContent_Declare(JUCE … GIT_TAG 8.0.15 GIT_SHALLOW TRUE SYSTEM)`.
Catch2 is fetched the same way (`v3.16.0`, tests only). No git submodules.

The exact tag lives in one place (`CMakeLists.txt`, variable `CLEARSPACE_JUCE_TAG`) and is
mirrored in the table at the top of `CLAUDE.md`. Bumping it is a deliberate commit with an
ADR if the bump is more than a patch release.

## Why FetchContent over a submodule

- **One command to build.** `cmake -B build` does everything; nobody has to remember
  `git submodule update --init --recursive`. Matters for CI runners and for a fresh
  clone on Seth's machine.
- **Pin is explicit and reviewable** in the same file that consumes it, rather than a
  detached commit SHA in `.gitmodules`.
- **No JUCE source in our history.** Repo stays small; JUCE is ~150 MB of history.
- **Offline / local override is built in:** `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`
  uses an existing checkout (e.g. a JUCE clone with local patches) without touching the
  build files. `-DFETCHCONTENT_UPDATES_DISCONNECTED=ON` skips network checks on re-configure.

## Why 8.0.15 and not 9.x

`CLAUDE.md` specifies JUCE 8.x. `8.0.15` is the last 8.x release tag at the time of writing.
JUCE 9.0.x exists; moving to it is a separate decision (new ADR) once a later phase needs
something it provides, so Phases 1–7 build on a stable, well-documented API.

## Trade-offs accepted

- Configure needs network access the first time (shallow clone of one tag, ~30–60 s).
  CI does this on every run unless a cache step is added later.
- FetchContent's `SYSTEM` keyword (CMake ≥ 3.25) is used so third-party headers don't
  trip our `-Werror`; `-Werror` is applied only to our own source files anyway.

## Alternatives considered

- **Git submodule at `external/JUCE`** — rejected for the reasons above; equally valid,
  just more friction.
- **System-installed JUCE (`find_package(JUCE)`)** — rejected: no pin, differs per machine.
