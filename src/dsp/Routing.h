#pragma once

// Signal-flow glue for SPEC §2: input/output trim, per-section level and click-free bypass,
// serial/parallel reverb feed, equal-power dry/wet mix, and tail reporting. Every gain is
// smoothed over 20 ms so no parameter change zippers. Pure DSP: no JUCE, no allocation after
// prepare(). The processor owns the buffers and calls these stages in order.

#include "Smoother.h"

namespace clearspace::dsp
{

struct RoutingParams
{
    float inputTrimDb = 0.0f;
    float outputTrimDb = 0.0f;
    float mixPercent = 50.0f;
    bool serial = true;
    bool delayBypass = false;
    bool reverbBypass = false;
    float delayLevelDb = 0.0f;
    float reverbLevelDb = 0.0f;
};

class Routing
{
public:
    static constexpr float smoothingMs = 20.0f;
    static constexpr double maxTailSeconds = 30.0;

    void prepare(double sampleRate);

    /** Sets smoother targets. Call once per block. */
    void setParams(const RoutingParams& params);

    /** Snaps every smoother to its target (use after prepare + the first setParams). */
    void reset();

    void applyInputTrim(float* const* buffer, int numChannels, int n);

    /** Section return gain: level × bypass crossfade. Apply before the ducker. */
    void applyDelayReturn(float* const* buffer, int numChannels, int n);
    void applyReverbReturn(float* const* buffer, int numChannels, int n);

    /** reverbIn = dry + serialGain · delayWet, where serialGain ramps between 0 and 1. */
    void buildReverbInput(const float* const* dry, const float* const* delayWet,
                          float* const* reverbIn, int numChannels, int n);

    /** out = dry·dryGain + (delayWet + reverbWet)·wetGain, then output trim. `out` may alias
        `dry`. */
    void mixOutput(const float* const* dry, const float* const* delayWet,
                   const float* const* reverbWet, float* const* out, int numChannels, int n);

    /** Equal-power crossfade: dry = cos(mix·π/2), wet = sin(mix·π/2), mix in 0…1. */
    static void equalPowerGains(float mix01, float& dryGain, float& wetGain);

    /** Seconds until the last echo above −60 dB has sounded. Capped at maxTailSeconds. */
    static double delayTailSeconds(double delayMs, double feedbackPercent);

    /** max(delay tail, reverb decay), each contributing 0 when bypassed. */
    static double tailSeconds(double delayMs, double feedbackPercent, bool delayBypass,
                              double reverbDecaySeconds, bool reverbBypass);

    static float dbToGain(float db);

private:
    static void applyGain(LinearSmoother& gain, float* const* buffer, int numChannels, int n);

    LinearSmoother inputGain, outputGain, dryGain, wetGain, delayReturnGain, reverbReturnGain,
        serialGain;
};

} // namespace clearspace::dsp
