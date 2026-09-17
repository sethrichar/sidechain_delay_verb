#pragma once

// Signal-flow assembly (SPEC §2): dry/wet split, serial/parallel effect order, per-section
// level + click-free bypass, duck gains on the returns, equal-power mix, output trim.
//
//   delayWet  = duckD · levelD · bypassD · Delay(in)
//   reverbIn  = serial ? in + delayWet : in
//   reverbWet = duckR · levelR · bypassR · Reverb(reverbIn)
//   out       = outTrim · (dry·cos(mix·π/2) + (delayWet + reverbWet)·sin(mix·π/2))
//
// Input trim is applied by the caller before the split, so "in" here is post-trim and is
// also what the internal ducker key sees. ADR-0002 covers the serial choice (the reverb
// receives the *ducked* delay return).

#include "Effect.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace clearspace::dsp
{

class Routing
{
public:
    struct Params
    {
        bool serial = true;
        float mixPercent = 50.0f;
        float outputTrimDb = 0.0f;
        bool delayBypass = false;
        float delayLevelDb = 0.0f;
        bool reverbBypass = false;
        float reverbLevelDb = 0.0f;
    };

    /** Ramp for trims, levels, mix and bypass. Bypass at ~20 ms per SPEC §2. */
    static constexpr float smoothingMs = 20.0f;
    /** Level knobs at their minimum are treated as "off" (gain 0), not −60 dB. */
    static constexpr float levelOffDb = -59.9f;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Sets smoother targets. Call once per block before process(). Realtime-safe. */
    void setParams(const Params& params) noexcept;

    /** `in` is stereo (post input trim). `delayGain`/`reverbGain` hold one linear ducker
        gain per sample. Writes stereo `out`, which may alias `in`. numSamples ≤ prepared. */
    void process(const float* inL, const float* inR, const float* delayGain,
                 const float* reverbGain, Effect& delay, Effect& reverb, float* outL, float* outR,
                 int numSamples) noexcept;

    /** Worst-case ring-out given the current params: serial → delay + reverb tails;
        parallel → the longer of the two. Bypassed sections contribute nothing. */
    double getTailSeconds(const Effect& delay, const Effect& reverb) const noexcept;

    /** Equal-power law (exposed for tests): dry = cos, wet = sin over the quarter circle. */
    static void mixGains(float mixPercent, float& dryGain, float& wetGain) noexcept;

private:
    struct Section
    {
        juce::LinearSmoothedValue<float> level{1.0f};
        juce::LinearSmoothedValue<float> bypassGain{1.0f}; // 1 = active, 0 = bypassed
        bool wasActive = true;

        /** True when the section still contributes (not fully faded out). */
        bool isActive() const noexcept
        {
            return bypassGain.isSmoothing() || bypassGain.getCurrentValue() > 0.0f;
        }
    };

    static float levelToGain(float dB) noexcept;

    /** Runs one section in place on the scratch pair; resets the effect on the frame where
        the bypass fade has completed. */
    void runSection(Section& section, Effect& effect, const float* duckGain, float* left,
                    float* right, int numSamples) noexcept;

    Params current;
    juce::LinearSmoothedValue<float> dryGain{0.70710678f};
    juce::LinearSmoothedValue<float> wetGain{0.70710678f};
    juce::LinearSmoothedValue<float> outputGain{1.0f};
    Section delaySection, reverbSection;

    juce::AudioBuffer<float> dryBuf, delayBuf, reverbBuf;
    int maxBlock = 0;
};

} // namespace clearspace::dsp
