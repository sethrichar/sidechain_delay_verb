#pragma once

// Delay engine (SPEC §4): the `Effect` the processor owns for the delay section. It selects
// the mode (Digital / BBD / Tape), applies the per-mode time clamps, converts tempo-sync note
// values to milliseconds, and reports the tail.
//
// Phase 2: only the Digital mode exists, so BBD and Tape run the Digital delay (with their
// own time clamps). Phase 5 adds the BBD and Tape models and the click-free 50 ms mode
// crossfade (BUILD_PLAN Phase 5).

#include "../Effect.h"
#include "DigitalDelay.h"

namespace clearspace::dsp
{

class DelayEngine final : public Effect
{
public:
    /** Index order = params::delayModeChoices(): Digital, BBD, Tape. */
    enum class Mode
    {
        digital = 0,
        bbd = 1,
        tape = 2
    };

    struct Params
    {
        Mode mode = Mode::digital;
        float timeMs = 375.0f;  // already resolved from delayTime or tempo sync by the caller
        float feedback = 0.35f; // 0…1
        float lowCutHz = 150.0f;
        float highCutHz = 8000.0f;
        float modDepth = 0.1f; // 0…1
        float modRateHz = 0.8f;
        bool pingPong = false;
    };

    /** Used when the host gives no tempo (Standalone, stopped transports without a BPM). */
    static constexpr double fallbackBpm = 120.0;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParams(const Params& params) noexcept;
    void process(float* left, float* right, int numSamples) noexcept override;

    /** Delay time limits per mode (SPEC §6): Digital 1–2000, BBD 20–1000, Tape 20–2000 ms. */
    static float clampTimeMs(Mode mode, float ms) noexcept;

    /** Length of one note in seconds. `noteIndex` follows params::delayNoteChoices()
        (1/64 … 1/1), `noteModIndex` params::delayNoteModChoices() (Straight, Dotted, Triplet).
        A BPM ≤ 0 uses `fallbackBpm`. Not clamped to the delay range. */
    static double noteLengthSeconds(double bpm, int noteIndex, int noteModIndex) noexcept;

    /** Tail for the given mode and settings; mirrors what setParams() publishes. */
    static double tailSecondsFor(Mode mode, float timeMs, float feedback) noexcept;

    const DigitalDelay& getDigital() const noexcept { return digital; }

private:
    DigitalDelay digital;
    Params current;
};

} // namespace clearspace::dsp
