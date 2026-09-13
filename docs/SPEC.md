# SPEC.md — Ducking Delay + Reverb

## 1. What this is

A stereo delay and reverb plugin with **built-in sidechain ducking**: the dry input is
split off and used as the key signal for a compressor on each effect return, so the
delay and reverb duck under the vocal and swell back up in the gaps. This is a common
manual technique in mixing and live audio (compressor on the reverb/delay return,
sidechained from the vocal); the plugin makes it a single insert with no routing.

Primary use cases, in priority order:
1. Insert on a vocal track (dry/wet mix in use).
2. Insert on an aux/return (100% wet; the send signal is the key).
3. Live console plugin hosting where the key may come from an **external sidechain bus**.

Non-goals for v1: multiband, mid/side, oversampling, lookahead, MIDI, AAX build.

## 2. Signal flow

```
                       ┌──────────────────────────────────────────────────────┐
in (L/R) ──┬──────────▶│ DRY                                                  │
           │           └──────────────────────────────────────────────────────┘
           │   key ──▶ [Key HPF] ──▶ [Detector] ──▶ Ducker(delay)   Ducker(reverb)
           │   (internal = in, or external sidechain bus)  │              │
           │                                               ▼              ▼
           ├──▶ [Delay engine] ──▶ [×delayLevel] ──▶ [Duck gain] ──┬──▶ delayWet
           │                                                       │
           │   reverbIn = (routing==Serial) ? in + delayWet : in   │
           └──▶ [Reverb engine] ─▶ [×reverbLevel] ─▶ [Duck gain] ──┼──▶ reverbWet
                                                                   ▼
out = dry·(1−mix) + (delayWet + reverbWet)·mix        (mix is an equal-power crossfade)
```

- Ducking is applied to the effect **return** (post feedback loop), not inside the
  loop. Optional "duck inside feedback loop" is a v2 idea; do not build it now.
- In Serial routing the reverb receives the **ducked** delay return plus dry input.
  (ADR this choice.)
- Global `mix` uses an equal-power law so 50% doesn't drop level.
- Bypass of a section is click-free (crossfade over ~20 ms).
- Tails: report `getTailLengthSeconds` = max(delay tail, reverb decay).

## 3. Ducker (shared module, one instance per effect)

Feed-forward compressor whose **detector input is the key** and whose **gain is applied
to the wet return**. Reference design: Giannoulis, Massberg & Reiss, *"Digital Dynamic
Range Compressor Design — A Tutorial and Analysis,"* JAES 60(6), 2012. Use the
log-domain feed-forward topology with smoothed gain (Figure 7 style):

1. Key = mono sum of the key source. Apply a 2nd-order HPF at `duckKeyHPF` so low-end
   (kick, plosives, rumble) doesn't trigger ducking.
2. Detector: RMS with ~8 ms integration window (default) — sounds more musical than
   peak for vocals. Convert to dB.
3. Gain computer: soft knee (6 dB), threshold `duckThreshold`. Instead of a ratio,
   expose **Depth** = maximum attenuation in dB; internally use a high ratio (20:1)
   and clamp the computed gain reduction to `−Depth`. This gives the "amount" knob
   users expect on a ducker.
4. Ballistics on the gain-reduction signal (in dB): attack coefficient from
   `duckAttack`, then **hold** (`duckHold`, gain held at minimum while the hold
   timer runs after the key drops below threshold), then release (`duckRelease`)
   as a one-pole exponential. The release is what produces the "swell". Make sure a
   re-trigger during release restarts attack cleanly.
5. Convert to linear and multiply the wet return (per channel, same gain both channels).
6. Publish current gain reduction (dB) to an atomic for the UI meter.

Global switches: `duckSource` (Internal input / External sidechain bus — declare a
sidechain bus in `BusesProperties`, fall back to internal if the host doesn't
connect it) and `duckLink` (reverb ducker mirrors delay ducker parameters; UI greys out
the reverb duck controls when linked).

Sensible defaults: Depth 12 dB, Threshold −30 dBFS, Attack 5 ms, Hold 60 ms,
Release 400 ms, Key HPF 120 Hz.

## 4. Delay engine

Common: stereo, fractional delay line with cubic Hermite interpolation, tempo sync
(`AudioPlayHead` BPM; note values 1/64 … 1/1 with straight/dotted/triplet), feedback
0–100 % (hard-clamped in code so runaway is impossible), stereo mode `Stereo` (L/R
independent, same time) or `Ping-Pong` (cross-fed feedback), level, bypass.

**Time-change behavior differs by mode (this is a deliberate sonic feature):**
- Digital: use **two delay lines and crossfade** (≈30 ms) when time changes, so there
  is no pitch artifact.
- BBD and Tape: **slew the read position** (one-pole, ~100 ms) so time changes produce
  the authentic pitch glide.

### 4.1 Digital mode — clean
- Delay 1 ms – 2000 ms. Feedback path: `delayLowCut` (2nd-order HPF) and
  `delayHighCut` (2nd-order LPF) so repeats can be shaped.
- `delayMod` (0–100 %) + `delayModRate`: gentle sine modulation of read position
  (chorus-like), depth up to ~2 ms. Off at 0.
- No saturation. Unity-gain loop with the filters is the only coloration.

### 4.2 BBD mode — bucket-brigade
Reference: Raffel & Smith, *"Practical Modeling of Bucket-Brigade Device Circuits,"*
DAFx-10, 2010; Holters & Parker, *"A Combined Model for a Bucket Brigade Device and its
Input and Output Filters,"* DAFx-18. Build the **behavioral** model, not a full
circuit sim:
- Emulate a 4096-stage chip (MN3005-class). Delay time `t` implies clock
  `f_clk = N / (2·t)`. Clamp delay to 20–1000 ms.
- **Bandwidth tracks the clock**: anti-aliasing + reconstruction filtering modeled as a
  4th-order lowpass with cutoff ≈ `f_clk / 3`, clamped to [1.5 kHz, 14 kHz]. Longer
  delay ⇒ darker, more lo-fi. This is the defining BBD sound.
- Soft saturation (tanh, gain staged by `delayDrive`) at the input and inside the
  feedback path; repeats degrade progressively.
- Compander artifact: an NE570-style compressor before the delay and expander after.
  Cheap approximation: a signal-dependent noise floor (filtered white noise inside the
  loop whose level rises as the envelope falls), giving the characteristic "breathing".
  `delayAge` (0–100 %) scales noise level and adds slight clock-jitter modulation.
- `delayMod`/`delayModRate` = small LFO on clock (subtle warble).
- Feedback tone filters from Digital mode are **not** exposed here; the clock filter
  is the tone.

### 4.3 Tape mode
Reference for design (do **not** copy code — GPL): Chowdhury, *"Real-Time Physical
Modelling for Analog Tape Machines,"* DAFx-19 (ChowTapeModel). Build a behavioral tape
echo (Space Echo / Echoplex character):
- Delay 20 ms – 2000 ms.
- Input stage: `delayDrive` into an asymmetric soft saturator (tanh with a small
  even-harmonic bias term). Consider 2× oversampling for this stage only if aliasing is
  measurable in tests; otherwise skip in v1.
- Head bump: peaking EQ, +2–3 dB around 80 Hz, Q ≈ 0.7.
- HF loss: 1st-order lowpass, cutoff from 12 kHz (`delayAge` = 0) down to ~4 kHz
  (`delayAge` = 100). Applied inside the loop so each repeat is darker.
- **Wow**: sine LFO 0.4–1.5 Hz. **Flutter**: 6–12 Hz LFO plus low-level filtered
  random. Both modulate read position; combined depth set by `delayMod`, rate offset
  by `delayModRate`. Default should be *audible but subtle*.
- Tape hiss: filtered noise at ≈ −70 dBFS, scaled by `delayAge`. Gated by an internal
  noise-gate so silence stays silent.
- Feedback path includes the saturator so high feedback compresses rather than runs away.

## 5. Reverb engine

Musicality goals: smooth (no metallic ringing, no flutter echoes), dense onset without
mud, decay that stays "in tune" with the source, and modulation that keeps long tails
alive. Three modes, each a separate class behind a common `ReverbEngine` interface with
shared pre-delay, low cut, high cut, width, level, and bypass.

### 5.1 Plate — Dattorro
Implement **Dattorro, "Effect Design, Part 1: Reverberator and Other Filters," JAES
45(9), 1997** — the figure-of-eight "tank" plate. This is the best-documented, most
widely praised plate-style algorithm and is the lineage of most commercial plates.
- Stages: pre-delay → input bandwidth LPF → 4 series input diffusion allpasses →
  two cross-coupled tank halves, each with a **modulated** allpass, delay, damping
  LPF, decay gain, second allpass, delay. Output L/R are sums of the paper's tap
  points.
- All delay lengths in the paper are given at **29.761 kHz**; scale them by
  `fs / 29761` and round to fractional samples (Hermite read).
- Map parameters: `reverbDecay` → decay gain (calibrate to RT60 with the render tool),
  `reverbDamping` → damping LPF, `reverbDiffusion` → input diffusion coefficients
  (0.75/0.625 at 100 %), `reverbSize` → scales tank delay lengths 0.5×–1.5×,
  `reverbModRate`/`reverbModDepth` → the tank allpass excursion (default ~8 samples
  @48k, ~1 Hz).
- Add an option internally for a second, slightly detuned modulation LFO per half so
  the two sides don't beat together.

### 5.2 Hall — modulated Feedback Delay Network
References: Jot & Chaigne, *"Digital Delay Networks for Designing Artificial
Reverberators,"* AES 90th Conv., 1991; Rocchesso & Smith, *"Circulant and Elliptic
Feedback Delay Networks for Artificial Reverberation,"* IEEE TSAP 1997; Geraint Luff,
*"Let's Write a Reverb"* (Signalsmith Audio, ADC 2021 — read the article, check the
code license before referencing any of it); Sean Costello's Valhalla DSP blog posts on
FDN modulation and diffusion.
- N = 8 delay lines (make N a template parameter; test 16 later). Lengths mutually
  prime, spread roughly 25–110 ms at `reverbSize` = 50 %, scaled 0.4×–2× by size.
- Feedback matrix: **Householder** (`I − (2/N)·11ᵀ`) — lossless, cheap, and dense.
  Keep a Hadamard variant behind a compile-time switch for A/B.
- Per-line frequency-dependent decay: a one-pole (or shelving) lowpass per line with
  cutoff from `reverbDamping`, and per-line gain
  `g_i = 10^(−3·L_i / (T60 · fs))` from `reverbDecay`, so all lines decay together.
- Input diffusion: 3–4 stages of multichannel diffuser (short per-channel delays +
  Hadamard mix + sign flips, Signalsmith-style) driven by `reverbDiffusion`.
- Modulation: each line's length modulated by its own slow LFO (0.1–2 Hz, ±2–8
  samples), rates mutually non-harmonic. This removes metallic resonances on long
  decays. `reverbModRate`/`reverbModDepth` scale all LFOs.
- Stereo: inject L into even lines and R into odd lines; output L = sum of one
  half-set, R = the other, then `reverbWidth` via mid/side scaling.
- Include a short (≈10–25 ms) early-reflection tap set for "hall bloom"; keep it subtle.

### 5.3 Room
Two acceptable designs; pick the first unless tests show a problem:
1. **Early reflections + small FDN**: a tapped delay line (12–16 taps, 5–60 ms, per-tap
   gain, stereo distribution, single LPF) for the room's early energy, feeding the same
   FDN engine as Hall but with short lengths (8–40 ms), low `reverbSize` range, RT60
   0.1–2.5 s, and heavier damping. ER level scales with `reverbSize`.
2. **Gardner nested allpass** structures (W. G. Gardner, *"The Virtual Acoustic Room,"*
   MIT thesis 1992 — small/medium/large room topologies).

Freeverb (Schroeder/Moorer comb+allpass, public domain) is a known reference point but
is **not** musical enough for this product; don't use it except as a test baseline.

## 6. Parameters (APVTS)

All IDs are `camelCase`, declared once in `Parameters.h`. Ranges use appropriate skew
(log for time/frequency). Defaults in **bold**.

### Global
| ID | Type | Range | Default | Notes |
|---|---|---|---|---|
| `inputTrim` | float dB | −24…+24 | **0** | |
| `outputTrim` | float dB | −24…+24 | **0** | |
| `mix` | float % | 0…100 | **50** | equal-power dry/wet |
| `routing` | choice | Serial, Parallel | **Serial** | |
| `duckSource` | choice | Internal, External | **Internal** | sidechain bus |
| `duckLink` | bool | | **on** | reverb ducker follows delay ducker |

### Delay
| ID | Type | Range | Default |
|---|---|---|---|
| `delayBypass` | bool | | off |
| `delayMode` | choice | Digital, BBD, Tape | **Digital** |
| `delayTime` | float ms | 1…2000 (log; BBD clamps 20–1000, Tape 20–2000) | **375** |
| `delaySync` | bool | | off |
| `delayNote` | choice | 1/64 … 1/1 | **1/8** |
| `delayNoteMod` | choice | Straight, Dotted, Triplet | **Straight** |
| `delayFeedback` | float % | 0…100 | **35** |
| `delayLowCut` | float Hz | 20…2000 (log) | **150** (Digital only) |
| `delayHighCut` | float Hz | 1000…20000 (log) | **8000** (Digital only) |
| `delayDrive` | float % | 0…100 | **20** (BBD/Tape only) |
| `delayAge` | float % | 0…100 | **30** (BBD/Tape only) |
| `delayMod` | float % | 0…100 | **10** |
| `delayModRate` | float Hz | 0.1…10 (log) | **0.8** |
| `delayStereoMode` | choice | Stereo, PingPong | **Stereo** |
| `delayLevel` | float dB | −60…+6 | **0** |
| `delayDuckEnable` | bool | | **on** |
| `delayDuckDepth` | float dB | 0…40 | **12** |
| `delayDuckThreshold` | float dBFS | −60…0 | **−30** |
| `delayDuckAttack` | float ms | 0.1…200 (log) | **5** |
| `delayDuckHold` | float ms | 0…500 | **60** |
| `delayDuckRelease` | float ms | 20…3000 (log) | **400** |
| `delayDuckKeyHPF` | float Hz | 20…1000 (log) | **120** |

### Reverb
| ID | Type | Range | Default |
|---|---|---|---|
| `reverbBypass` | bool | | off |
| `reverbMode` | choice | Plate, Hall, Room | **Plate** |
| `reverbPreDelay` | float ms | 0…250 | **20** |
| `reverbDecay` | float s | 0.1…20 (log; Room clamps ≤ 2.5) | **2.0** |
| `reverbSize` | float % | 0…100 | **50** |
| `reverbDamping` | float Hz | 1000…20000 (log) | **6000** |
| `reverbLowCut` | float Hz | 20…500 (log) | **100** |
| `reverbHighCut` | float Hz | 1000…20000 (log) | **12000** |
| `reverbDiffusion` | float % | 0…100 | **80** |
| `reverbModRate` | float Hz | 0.1…5 (log) | **1.0** |
| `reverbModDepth` | float % | 0…100 | **30** |
| `reverbWidth` | float % | 0…100 | **100** |
| `reverbLevel` | float dB | −60…+6 | **0** |
| `reverbDuck*` | same seven as `delayDuck*` | | same defaults |

Mode-specific parameters that don't apply are hidden in the UI (not removed from
APVTS) so automation and presets stay stable.

## 7. UI

Aesthetic targets: Valhalla DSP, Cradle Audio, Ableton stock/Max for Live devices —
**flat, minimal, typographic, one accent color, no skeuomorphism, no bitmaps.**
Everything drawn in vector via a custom `juce::LookAndFeel_V4` subclass.

- Base size 860 × 440, resizable with fixed aspect ratio (`setResizeLimits`, scale
  via `AudioProcessorEditor::setScaleFactor` or a transform), min 0.75×, max 2×.
- Layout, left to right: **DELAY** panel | **REVERB** panel | narrow **GLOBAL** strip.
  Each effect panel has: header row (section name, mode segmented control, bypass),
  a row of knobs for the effect, and a **DUCK** sub-row beneath (Depth, Threshold,
  Attack, Hold, Release, Key HPF) with a horizontal **gain-reduction meter** — the GR
  meter is the signature UI element of this product; make it readable from across a
  room. Global strip: Mix (large), Input/Output trim, Routing, Duck Source, Duck Link.
- Knobs: thin circular track, accent-colored value arc from the knob's zero point,
  small indicator dot; label above in caps (11 px, letter-spaced), value below
  (13 px, units). Double-click resets. Shift-drag = fine.
- Segmented controls for modes (text buttons in a pill; selected = accent fill).
- Typography: one sans-serif. Prefer embedding **Inter** (SIL OFL — record in
  THIRD_PARTY_NOTICES) via `BinaryData`; fall back to the system font.
- Design tokens in `LookAndFeel.h` as `constexpr` colours: background (near-white
  `#F4F3F0`), panel (`#FFFFFF`), hairline (`#DDDAD4`), text (`#1F1E1C`), muted text
  (`#8A867F`), accent (`#E8632B`), meter (`#1F1E1C`). Dark theme is v2.
- Section dividers are 1-px hairlines, not boxes. Generous whitespace. No drop shadows.
- Preset bar across the top: preset name, prev/next, save. Factory presets in
  `presets/` (aim for 8: e.g. "Vocal Slap Duck", "Plate Vocal", "Tape Throw",
  "BBD Ambience", "Hall Swell", "Room Tight", "Return 100% Wet", "Init").
- UI must work at 30 Hz meter refresh with no allocation in `paint`.
