#pragma once

// Plate reverb (SPEC §5.1): Dattorro's figure-of-eight "tank", implemented from
// J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", JAES 45(9), 1997.
//
//   Input          mono sum of L/R → four series diffusion allpasses (142 / 107 / 379 / 277
//                  samples at the paper's 29.761 kHz, coefficients 0.75 / 0.75 / 0.625 / 0.625
//                  scaled by `diffusion`).
//   Tank           two cross-coupled halves, each: modulated allpass (672 | 908, −0.7) →
//                  delay (4453 | 4217) → one-pole damping LPF → × decay → allpass
//                  (1800 | 2656, +0.5) → delay (3720 | 3163) → × decay → the other half.
//   Output         L and R are the paper's seven tap points each (Table 2), scaled by 0.6.
//   Lengths        every tank length is scaled by fs / 29761 and by `size` (0.5×–1.5×),
//                  read with Hermite interpolation. Size changes glide over `sizeGlideMs`.
//   Decay          the loop gain is set so the measured RT60 matches `decaySeconds`
//                  (`decayGainFor`), and every allpass coefficient is capped so its own tail
//                  can never outlast the requested decay (`allpassCoefficientFor`).
//   Modulation     one sine LFO per half, the right half detuned by `rightLfoRatio`, driving
//                  the first allpass of each half by up to 16 samples (at 29.761 kHz).
//
// Design notes, calibration and alternatives: docs/decisions/ADR-0005-plate-reverb.md.
// Pure DSP: no allocation in process(), all sizing in prepare().

#include "../DelayLine.h"
#include "../Effect.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>

namespace clearspace::dsp
{

class PlateReverb final : public Effect
{
public:
    struct Params
    {
        float decaySeconds = 2.0f; // RT60 target
        float size = 0.5f;         // 0…1 (the `reverbSize` percentage / 100)
        float dampingHz = 6000.0f; // tank damping LPF cutoff
        float diffusion = 0.8f;    // 0…1 (the `reverbDiffusion` percentage / 100)
        float modRateHz = 1.0f;
        float modDepth = 0.3f; // 0…1 (the `reverbModDepth` percentage / 100)
    };

    /** The paper's design sample rate; all lengths below are in samples at this rate. */
    static constexpr double referenceRate = 29761.0;

    static constexpr float minDecaySeconds = 0.1f;
    static constexpr float maxDecaySeconds = 20.0f;
    /** `size` 0…1 maps to this scale on every tank length. */
    static constexpr float minSizeScale = 0.5f;
    static constexpr float maxSizeScale = 1.5f;
    /** Modulated-allpass excursion at `modDepth` = 1, in samples at `referenceRate`
        (the paper's 16; ~8 samples at 48 kHz at the 30 % default). */
    static constexpr float maxExcursionReference = 16.0f;
    /** The right half's LFO runs at this multiple of the left's so the halves never beat. */
    static constexpr double rightLfoRatio = 1.07;
    /** Size changes glide the tank read positions over this time (pitch-smears, no click). */
    static constexpr float sizeGlideMs = 300.0f;
    /** Ramp for decay gain, damping, diffusion coefficients and modulation depth. */
    static constexpr float smoothingMs = 20.0f;
    /** Loop gain ceiling; the analytic value stays below it for every decay/size, this only
        guards a bad parameter. */
    static constexpr float maxDecayGain = 0.995f;
    /** Output scaling (the paper's 0.6, then trimmed so a 2 s plate sits near unity RMS on a
        sustained source). */
    static constexpr float outputGain = 0.6f * 0.5f;
    /** Measured-RT60 correction: the loop's mean round trip is scaled by this before the
        decay gain is computed (calibrated at the 6 kHz damping default with the hidden
        "[.calibrate]" case in tests/test_reverb.cpp, see ADR-0005). */
    static constexpr double decayCalibration = 0.92;
    /** Every allpass coefficient is capped so its own ring-out is at most this fraction of
        the requested decay; cascaded stages decaying at the same rate would otherwise read
        long on a Schroeder fit at short decays. */
    static constexpr double allpassRingoutFraction = 0.5;
    /** Tail = the tank's full round trip (propagation to the last tap) + this × decay. */
    static constexpr double tailDecayMultiplier = 1.25;
    /** The tank's round trip is never longer than this × the decay: a 0.3 s decay on a
        1 s tank is early reflections, not a decay, so short decays shrink the tank
        (`effectiveSizeScale`). Only bites below ~0.9 s at full size. */
    static constexpr double maxLoopToDecayRatio = 1.2;

    // Paper lengths at `referenceRate`, exposed for the tests.
    static constexpr std::array<int, 4> inputDiffusionLengths = {142, 107, 379, 277};
    static constexpr std::array<float, 2> inputDiffusionCoefficients = {0.75f, 0.625f};
    static constexpr std::array<int, 2> tankAllpass1Lengths = {672, 908}; // L, R (modulated)
    static constexpr std::array<int, 2> tankDelay1Lengths = {4453, 4217};
    static constexpr std::array<int, 2> tankAllpass2Lengths = {1800, 2656};
    static constexpr std::array<int, 2> tankDelay2Lengths = {3720, 3163};
    static constexpr float tankAllpass1Coefficient = -0.7f;
    static constexpr float tankAllpass2Coefficient = 0.5f;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;

    /** Sets targets for the next process() call. Realtime-safe; call once per block. */
    void setParams(const Params& params) noexcept;

    /** In place. The input is summed to mono; the output is the stereo plate return. */
    void process(float* left, float* right, int numSamples) noexcept override;

    /** Ring-out after the input stops (≥ 60 dB down): the tank's round trip at this size
        (the impulse takes that long to reach every tap) plus `tailDecayMultiplier` × decay
        for the slowest-decaying modes. `size` is 0…1. */
    static double tailSecondsFor(float decaySeconds, float size) noexcept;

    /** Size scale for a `size` of 0…1, before the short-decay coupling. */
    static float sizeScaleFor(float size) noexcept
    {
        return minSizeScale + std::clamp(size, 0.0f, 1.0f) * (maxSizeScale - minSizeScale);
    }

    /** The size scale actually used: `sizeScaleFor(size)` limited so the round trip stays
        within `maxLoopToDecayRatio` × decay (never below `minSizeScale`). */
    static float effectiveSizeScale(float decaySeconds, float size) noexcept;

    /** Time around the whole figure-eight (all four delays and four tank allpasses at their
        mean group delay) for a size scale, in seconds. */
    static double loopSecondsFor(float sizeScale) noexcept;

    /** Loop gain per decay stage for a target RT60, given the mean time between two decay
        stages (`stageSeconds`). 10^(−3·stageSeconds·decayCalibration / RT60). */
    static float decayGainFor(float decaySeconds, double stageSeconds) noexcept;

    /** An allpass of `lengthSeconds` with coefficient k rings for 3·length/−log10|k| seconds
        to −60 dB; this returns `nominal` reduced (toward 0) so that ring-out never exceeds
        `allpassRingoutFraction` × the requested decay. */
    static float allpassCoefficientFor(float nominal, double lengthSeconds,
                                       float decaySeconds) noexcept;

    /** Mean time between decay stages for a size scale, in seconds (`loopSecondsFor`
        divided by the four decay gains around it). */
    static double stageSecondsFor(float sizeScale) noexcept;

    /** Current size scale (smoothed), exposed for tests. */
    float getCurrentSizeScale() const noexcept { return sizeScale.getCurrentValue(); }

private:
    /** Schroeder allpass on a fractional line: H(z) = (−k + z^−N) / (1 − k·z^−N). */
    struct Allpass
    {
        DelayLine<float> line;

        float tickHermite(float x, float delaySamples, float k) noexcept
        {
            const float d = line.readHermite(delaySamples);
            const float v = x + k * d;
            line.write(v);
            return d - k * v;
        }

        float tickInteger(float x, int delaySamples, float k) noexcept
        {
            const float d = line.read(delaySamples);
            const float v = x + k * d;
            line.write(v);
            return d - k * v;
        }
    };

    struct Half
    {
        Allpass allpass1; // modulated
        DelayLine<float> delay1;
        Allpass allpass2;
        DelayLine<float> delay2;
        float damperState = 0.0f;
        double lfoPhase = 0.0;
        double lfoIncrement = 0.0;
    };

    float toSamples(int referenceLength) const noexcept
    {
        return static_cast<float>(referenceLength * fs / referenceRate);
    }
    void updateCoefficients() noexcept;

    double fs = 48000.0;
    Params current;

    std::array<Allpass, 4> inputDiffusers;
    std::array<int, 4> inputDiffuserDelays{1, 1, 1, 1};
    std::array<Half, 2> halves;

    juce::LinearSmoothedValue<float> sizeScale{1.0f};
    juce::LinearSmoothedValue<float> decayGain{0.5f};
    juce::LinearSmoothedValue<float> dampingCoeff{0.0f};
    juce::LinearSmoothedValue<float> excursionSamples{0.0f};
    std::array<juce::LinearSmoothedValue<float>, 2> inputCoeff;
    std::array<juce::LinearSmoothedValue<float>, 2> tank1Coeff; // per half
    std::array<juce::LinearSmoothedValue<float>, 2> tank2Coeff; // per half
};

} // namespace clearspace::dsp
