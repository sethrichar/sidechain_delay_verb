#pragma once

// Reverb engine (SPEC §5): the `Effect` the processor owns for the reverb section. It selects
// the mode (Plate / Hall / Room), applies the per-mode decay clamp, and wraps the algorithm
// with the shared stages: pre-delay → algorithm → low cut → high cut → width.
//
// Phase 3: only the Plate exists, so Hall and Room run the Plate (with the Room's decay
// clamp). Phase 4 adds the Hall FDN and the Room (BUILD_PLAN Phase 4).

#include "../Biquad.h"
#include "../DelayLine.h"
#include "../Effect.h"
#include "PlateReverb.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace clearspace::dsp
{

class ReverbEngine final : public Effect
{
public:
    /** Index order = params::reverbModeChoices(): Plate, Hall, Room. */
    enum class Mode
    {
        plate = 0,
        hall = 1,
        room = 2
    };

    struct Params
    {
        Mode mode = Mode::plate;
        float preDelayMs = 20.0f;
        float decaySeconds = 2.0f;
        float size = 0.5f;         // 0…1
        float dampingHz = 6000.0f; // tank damping LPF
        float lowCutHz = 100.0f;   // on the return
        float highCutHz = 12000.0f;
        float diffusion = 0.8f; // 0…1
        float modRateHz = 1.0f;
        float modDepth = 0.3f; // 0…1
        float width = 1.0f;    // 0…1 (0 = mono, 1 = the algorithm's own stereo)
    };

    static constexpr float maxPreDelayMs = 250.0f;
    /** SPEC §6: Room clamps the decay to this. */
    static constexpr float roomMaxDecaySeconds = 2.5f;
    /** Pre-delay changes glide the read position over this time (click-free). */
    static constexpr float preDelayGlideMs = 100.0f;
    /** Ramp for width and the return filters' cutoffs. */
    static constexpr float smoothingMs = 20.0f;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void setParams(const Params& params) noexcept;
    void process(float* left, float* right, int numSamples) noexcept override;

    /** Decay limits per mode (SPEC §6): 0.1–20 s, Room ≤ 2.5 s. */
    static float clampDecaySeconds(Mode mode, float seconds) noexcept;

    /** Tail for the given mode and settings (`size` 0…1); mirrors what setParams()
        publishes. */
    static double tailSecondsFor(Mode mode, float decaySeconds, float preDelayMs,
                                 float size) noexcept;

    const PlateReverb& getPlate() const noexcept { return plate; }

private:
    void updateFilters(int numSamples) noexcept;
    float readPreDelay(const DelayLine<float>& line, float x, float delaySamples) const noexcept;

    double fs = 48000.0;
    Params current;

    PlateReverb plate;

    std::array<DelayLine<float>, 2> preDelayLines;
    std::array<Biquad, 2> lowCut, highCut;

    juce::LinearSmoothedValue<float> preDelaySamples{0.0f};
    juce::LinearSmoothedValue<float> width{1.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCutHz{100.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> highCutHz{12000.0f};
    float appliedLowCutHz = -1.0f;
    float appliedHighCutHz = -1.0f;
};

} // namespace clearspace::dsp
