#include "PlateReverb.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace clearspace::dsp
{

namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;

// Output tap points from the paper's Table 2 (samples at referenceRate). Node names follow
// the paper: 24_30 / 48_54 = first tank delays, 31_33 / 55_59 = second tank allpasses,
// 33_39 / 59_63 = second tank delays; the first index is the left half.
struct Tap
{
    int node; // 0 delay1, 1 allpass2, 2 delay2
    int half; // 0 left, 1 right
    int position;
    float sign;
};

constexpr std::array<Tap, 7> leftTaps = {{{0, 1, 266, 1.0f},
                                          {0, 1, 2974, 1.0f},
                                          {1, 1, 1913, -1.0f},
                                          {2, 1, 1996, 1.0f},
                                          {0, 0, 1990, -1.0f},
                                          {1, 0, 187, -1.0f},
                                          {2, 0, 1066, -1.0f}}};

constexpr std::array<Tap, 7> rightTaps = {{{0, 0, 353, 1.0f},
                                           {0, 0, 3627, 1.0f},
                                           {1, 0, 1228, -1.0f},
                                           {2, 0, 2673, 1.0f},
                                           {0, 1, 2111, -1.0f},
                                           {1, 1, 335, -1.0f},
                                           {2, 1, 121, -1.0f}}};

/** Sum of every tank length (both halves) at referenceRate. */
constexpr int tankTotalReference()
{
    int total = 0;
    for (size_t h = 0; h < 2; ++h)
        total += PlateReverb::tankAllpass1Lengths[h] + PlateReverb::tankDelay1Lengths[h] +
                 PlateReverb::tankAllpass2Lengths[h] + PlateReverb::tankDelay2Lengths[h];
    return total;
}
} // namespace

float PlateReverb::decayGainFor(float decaySeconds, double stageSeconds) noexcept
{
    const double rt60 = std::clamp(decaySeconds, minDecaySeconds, maxDecaySeconds);
    const double g = std::pow(10.0, -3.0 * stageSeconds * decayCalibration / rt60);
    return static_cast<float>(std::clamp(g, 0.0, static_cast<double>(maxDecayGain)));
}

float PlateReverb::allpassCoefficientFor(float nominal, double lengthSeconds,
                                         float decaySeconds) noexcept
{
    const double rt60 = std::clamp(decaySeconds, minDecaySeconds, maxDecaySeconds);
    const double cap = std::pow(10.0, -3.0 * lengthSeconds / (allpassRingoutFraction * rt60));
    const double magnitude = std::min(static_cast<double>(std::abs(nominal)), cap);
    return static_cast<float>(std::copysign(magnitude, static_cast<double>(nominal)));
}

double PlateReverb::loopSecondsFor(float sizeScaleValue) noexcept
{
    // Lengths scale with fs/referenceRate, so in seconds they are simply the reference
    // lengths over referenceRate.
    return static_cast<double>(tankTotalReference()) * sizeScaleValue / referenceRate;
}

double PlateReverb::stageSecondsFor(float sizeScaleValue) noexcept
{
    // Four decay gains per figure-eight round trip.
    return loopSecondsFor(sizeScaleValue) / 4.0;
}

float PlateReverb::effectiveSizeScale(float decaySeconds, float size) noexcept
{
    const double decay = std::clamp(decaySeconds, minDecaySeconds, maxDecaySeconds);
    const double limit = maxLoopToDecayRatio * decay / loopSecondsFor(1.0f);
    return static_cast<float>(std::clamp(std::min(static_cast<double>(sizeScaleFor(size)), limit),
                                         static_cast<double>(minSizeScale),
                                         static_cast<double>(maxSizeScale)));
}

double PlateReverb::tailSecondsFor(float decaySeconds, float size) noexcept
{
    // At a large size the impulse needs most of a round trip just to reach the last tap, and
    // the modes with the longest allpass group delay decay slower than the fitted RT60.
    // Measured (tests/test_reverb.cpp): ≥ 60 dB down at this time for every decay and size.
    const double decay = std::clamp(decaySeconds, minDecaySeconds, maxDecaySeconds);
    return loopSecondsFor(effectiveSizeScale(decaySeconds, size)) + tailDecayMultiplier * decay;
}

void PlateReverb::prepare(double sampleRate, int)
{
    fs = sampleRate;

    const auto capacity = [this](int referenceLength, float scale, float extraReference)
    {
        return static_cast<int>(
                   std::ceil(toSamples(referenceLength) * scale + toSamples(1) * extraReference)) +
               4;
    };

    for (size_t i = 0; i < inputDiffusers.size(); ++i)
    {
        inputDiffuserDelays[i] =
            std::max(1, static_cast<int>(std::lround(toSamples(inputDiffusionLengths[i]))));
        inputDiffusers[i].line.prepare(inputDiffuserDelays[i]);
    }

    for (size_t h = 0; h < halves.size(); ++h)
    {
        auto& half = halves[h];
        half.allpass1.line.prepare(
            capacity(tankAllpass1Lengths[h], maxSizeScale, maxExcursionReference));
        half.delay1.prepare(capacity(tankDelay1Lengths[h], maxSizeScale, 0.0f));
        half.allpass2.line.prepare(capacity(tankAllpass2Lengths[h], maxSizeScale, 0.0f));
        half.delay2.prepare(capacity(tankDelay2Lengths[h], maxSizeScale, 0.0f));
    }

    sizeScale.reset(sampleRate, sizeGlideMs * 1.0e-3);
    const double ramp = smoothingMs * 1.0e-3;
    decayGain.reset(sampleRate, ramp);
    dampingCoeff.reset(sampleRate, ramp);
    excursionSamples.reset(sampleRate, ramp);
    for (auto* group : {&inputCoeff, &tank1Coeff, &tank2Coeff})
        for (auto& s : *group)
            s.reset(sampleRate, ramp);

    setParams(current);
    reset();
}

void PlateReverb::reset() noexcept
{
    for (auto& ap : inputDiffusers)
        ap.line.clear();
    for (auto& half : halves)
    {
        half.allpass1.line.clear();
        half.delay1.clear();
        half.allpass2.line.clear();
        half.delay2.clear();
        half.damperState = 0.0f;
        half.lfoPhase = 0.0;
    }
    halves[1].lfoPhase = 0.5 * std::numbers::pi; // quadrature start so the halves differ at once

    // Snap every smoother, then recompute the size-dependent targets from the snapped size
    // and snap again so nothing ramps out of a stale value.
    const auto snapAll = [this]
    {
        sizeScale.setCurrentAndTargetValue(sizeScale.getTargetValue());
        decayGain.setCurrentAndTargetValue(decayGain.getTargetValue());
        dampingCoeff.setCurrentAndTargetValue(dampingCoeff.getTargetValue());
        excursionSamples.setCurrentAndTargetValue(excursionSamples.getTargetValue());
        for (auto* group : {&inputCoeff, &tank1Coeff, &tank2Coeff})
            for (auto& s : *group)
                s.setCurrentAndTargetValue(s.getTargetValue());
    };
    snapAll();
    updateCoefficients();
    snapAll();
}

void PlateReverb::setParams(const Params& params) noexcept
{
    current = params;
    current.decaySeconds = std::clamp(params.decaySeconds, minDecaySeconds, maxDecaySeconds);
    current.size = std::clamp(params.size, 0.0f, 1.0f);
    current.diffusion = std::clamp(params.diffusion, 0.0f, 1.0f);
    current.modDepth = std::clamp(params.modDepth, 0.0f, 1.0f);

    sizeScale.setTargetValue(effectiveSizeScale(current.decaySeconds, current.size));
    updateCoefficients();
    setTailSeconds(tailSecondsFor(current.decaySeconds, current.size));
}

void PlateReverb::updateCoefficients() noexcept
{
    // Everything that depends on the (gliding) size is derived from its current value, so
    // the RT60 stays put while the tank stretches.
    const float sz = sizeScale.getCurrentValue();
    const float decay = current.decaySeconds;

    decayGain.setTargetValue(decayGainFor(decay, stageSecondsFor(sz)));

    const double fc = std::clamp(static_cast<double>(current.dampingHz), 1.0, 0.49 * fs);
    dampingCoeff.setTargetValue(static_cast<float>(std::exp(-twoPi * fc / fs)));

    excursionSamples.setTargetValue(current.modDepth * toSamples(1) * maxExcursionReference);

    for (size_t i = 0; i < inputCoeff.size(); ++i)
    {
        // The longer of each coefficient's two diffusers bounds the cap.
        const int longest =
            std::max(inputDiffusionLengths[2 * i], inputDiffusionLengths[2 * i + 1]);
        inputCoeff[i].setTargetValue(
            allpassCoefficientFor(inputDiffusionCoefficients[i] * current.diffusion,
                                  static_cast<double>(longest) / referenceRate, decay));
    }

    for (size_t h = 0; h < halves.size(); ++h)
    {
        const double seconds1 = static_cast<double>(tankAllpass1Lengths[h]) * sz / referenceRate;
        const double seconds2 = static_cast<double>(tankAllpass2Lengths[h]) * sz / referenceRate;
        tank1Coeff[h].setTargetValue(
            allpassCoefficientFor(tankAllpass1Coefficient, seconds1, decay));
        tank2Coeff[h].setTargetValue(
            allpassCoefficientFor(tankAllpass2Coefficient, seconds2, decay));

        const double rate = std::clamp(static_cast<double>(current.modRateHz), 0.0, 0.25 * fs) *
                            (h == 0 ? 1.0 : rightLfoRatio);
        halves[h].lfoIncrement = twoPi * rate / fs;
    }
}

void PlateReverb::process(float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    updateCoefficients();

    const float unit = toSamples(1); // samples per reference sample
    auto& L = halves[0];
    auto& R = halves[1];

    for (int i = 0; i < numSamples; ++i)
    {
        const float sz = sizeScale.getNextValue() * unit; // reference samples → samples
        const float g = decayGain.getNextValue();
        const float damp = dampingCoeff.getNextValue();
        const float exc = excursionSamples.getNextValue();
        const float ic1 = inputCoeff[0].getNextValue();
        const float ic2 = inputCoeff[1].getNextValue();

        // Input: mono sum → four series diffusion allpasses (fixed, integer lengths).
        float x = 0.5f * (left[i] + right[i]);
        x = inputDiffusers[0].tickInteger(x, inputDiffuserDelays[0], ic1);
        x = inputDiffusers[1].tickInteger(x, inputDiffuserDelays[1], ic1);
        x = inputDiffusers[2].tickInteger(x, inputDiffuserDelays[2], ic2);
        x = inputDiffusers[3].tickInteger(x, inputDiffuserDelays[3], ic2);

        // Cross-coupling: each half's input is the diffused signal plus the decayed end of
        // the other half. Read both ends before anything is written this sample.
        const float endL = L.delay2.readHermite(static_cast<float>(tankDelay2Lengths[0]) * sz);
        const float endR = R.delay2.readHermite(static_cast<float>(tankDelay2Lengths[1]) * sz);

        for (size_t h = 0; h < 2; ++h)
        {
            auto& half = halves[h];
            float s = x + g * (h == 0 ? endR : endL);

            const float lfo = static_cast<float>(std::sin(half.lfoPhase));
            half.lfoPhase += half.lfoIncrement;
            if (half.lfoPhase >= twoPi)
                half.lfoPhase -= twoPi;

            s = half.allpass1.tickHermite(
                s, static_cast<float>(tankAllpass1Lengths[h]) * sz + exc * lfo,
                tank1Coeff[h].getNextValue());

            const float d1 = half.delay1.readHermite(static_cast<float>(tankDelay1Lengths[h]) * sz);
            half.delay1.write(s);

            half.damperState += (1.0f - damp) * (d1 - half.damperState);
            s = half.damperState * g;

            s = half.allpass2.tickHermite(s, static_cast<float>(tankAllpass2Lengths[h]) * sz,
                                          tank2Coeff[h].getNextValue());
            half.delay2.write(s);
        }

        // Output taps.
        const auto tap = [&](const Tap& t)
        {
            auto& half = halves[static_cast<size_t>(t.half)];
            const float position = static_cast<float>(t.position) * sz;
            const DelayLine<float>& line =
                t.node == 0 ? half.delay1 : (t.node == 1 ? half.allpass2.line : half.delay2);
            return t.sign * line.readHermite(position);
        };
        float yL = 0.0f, yR = 0.0f;
        for (const auto& t : leftTaps)
            yL += tap(t);
        for (const auto& t : rightTaps)
            yR += tap(t);

        left[i] = outputGain * yL;
        right[i] = outputGain * yR;
    }
}

} // namespace clearspace::dsp
