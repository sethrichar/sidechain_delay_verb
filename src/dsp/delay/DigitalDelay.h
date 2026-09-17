#pragma once

// Digital delay (SPEC §4.1): clean stereo delay, 1–2000 ms, on a fractional Hermite line.
//
//   Time changes   two read heads on one buffer, equal-power crossfade over 30 ms, so a new
//                  time never pitch-shifts (SPEC §4: "two delay lines and crossfade"). A
//                  change that arrives mid-crossfade is picked up when the crossfade ends.
//   Feedback       0–100 % from the user, hard-clamped to `maxFeedback` so the loop can never
//                  run away; the loop is otherwise unity gain (no saturation).
//   Feedback tone  2nd-order HPF (`lowCut`) and LPF (`highCut`) in the feedback path only, so
//                  the first echo is untouched and each repeat gets more shaped.
//   Modulation     sine LFO on the read position, depth up to `maxModMs`, right channel in
//                  quadrature with the left.
//   Stereo         Stereo = independent L/R lines, same time. PingPong = mono sum into the
//                  left line, left return feeds the right line and vice versa, so successive
//                  echoes alternate L, R, L, … The switch is a 20 ms blend, not a jump.
//
// Design notes and alternatives: docs/decisions/ADR-0004-digital-delay.md.

#include "../Biquad.h"
#include "../DelayLine.h"
#include "../Effect.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace clearspace::dsp
{

class DigitalDelay final : public Effect
{
public:
    struct Params
    {
        float timeMs = 375.0f;
        float feedback = 0.35f; // 0…1 (the `delayFeedback` percentage / 100)
        float lowCutHz = 150.0f;
        float highCutHz = 8000.0f;
        float modDepth = 0.1f; // 0…1 (the `delayMod` percentage / 100)
        float modRateHz = 0.8f;
        bool pingPong = false;
    };

    static constexpr float minTimeMs = 1.0f;
    static constexpr float maxTimeMs = 2000.0f;
    /** Read-position modulation at `modDepth` = 1. */
    static constexpr float maxModMs = 2.0f;
    /** Equal-power crossfade between the two read heads on a time change. */
    static constexpr float crossfadeMs = 30.0f;
    /** Feedback coefficient ceiling: 100 % on the knob maps here (ADR-0004). */
    static constexpr float maxFeedback = 0.98f;
    /** Ramp for feedback, mod depth, filter cutoffs and the stereo-mode blend. */
    static constexpr float smoothingMs = 20.0f;
    /** Two time targets closer than this (in samples) are treated as equal. */
    static constexpr float retargetThresholdSamples = 0.01f;
    /** A delay within this many samples of an integer is read as that integer (see .cpp):
        0.1 sample is ~2 µs at 48 kHz, below the time knob's own display resolution. */
    static constexpr double integerSnapSamples = 0.1;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;

    /** Sets targets for the next process() call. Realtime-safe; call once per block. */
    void setParams(const Params& params) noexcept;

    void process(float* left, float* right, int numSamples) noexcept override;

    /** Ring-out after the input stops: the (modulated) delay time, times the number of repeats
        it takes the clamped feedback to fall by 60 dB. */
    static double tailSecondsFor(float timeMs, float feedback) noexcept;

    /** True while a time change is being crossfaded. Exposed for tests. */
    bool isCrossfading() const noexcept { return crossfading; }

private:
    struct Channel
    {
        DelayLine<float> line;
        Biquad lowCut, highCut;
    };

    float timeToSamples(float ms) const noexcept;
    void updateFilters(int numSamples) noexcept;
    void maybeStartCrossfade() noexcept;

    double fs = 48000.0;
    Params current;

    std::array<Channel, 2> channels;
    int maxDelaySamples = 0;

    // Two read heads on the same buffer. `activeHead` is the one currently sounding; the other
    // is set to the new time and faded in when the time changes.
    std::array<float, 2> headDelaySamples{0.0f, 0.0f};
    int activeHead = 0;
    float requestedDelaySamples = 0.0f;
    bool crossfading = false;
    int crossfadeLength = 1;
    int crossfadeRemaining = 0;
    float fadeCos = 1.0f, fadeSin = 0.0f;       // current crossfade gains (out, in)
    float fadeRotCos = 1.0f, fadeRotSin = 0.0f; // per-sample rotation

    // Modulation
    double lfoPhase = 0.0;
    double lfoIncrement = 0.0;
    juce::LinearSmoothedValue<float> modDepthSamples{0.0f};

    // Loop
    juce::LinearSmoothedValue<float> feedbackGain{0.0f};
    juce::LinearSmoothedValue<float> pingPongBlend{0.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCutHz{150.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> highCutHz{8000.0f};
    float appliedLowCutHz = -1.0f;
    float appliedHighCutHz = -1.0f;
};

} // namespace clearspace::dsp
