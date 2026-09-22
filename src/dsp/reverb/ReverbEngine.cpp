#include "ReverbEngine.h"

#include <algorithm>
#include <cmath>

namespace clearspace::dsp
{

namespace
{
/** Filter coefficients are only recomputed when a cutoff has moved by more than this. */
constexpr float cutoffChangeThresholdHz = 1.0e-3f;
} // namespace

float ReverbEngine::clampDecaySeconds(Mode mode, float seconds) noexcept
{
    const float hi = mode == Mode::room ? roomMaxDecaySeconds : PlateReverb::maxDecaySeconds;
    return std::clamp(seconds, PlateReverb::minDecaySeconds, hi);
}

double ReverbEngine::tailSecondsFor(Mode mode, float decaySeconds, float preDelayMs,
                                    float size) noexcept
{
    // Phase 3: every mode is the Plate.
    const double preDelay = std::clamp(preDelayMs, 0.0f, maxPreDelayMs) * 1.0e-3;
    return preDelay + PlateReverb::tailSecondsFor(clampDecaySeconds(mode, decaySeconds), size);
}

void ReverbEngine::prepare(double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    const int maxPreDelay = static_cast<int>(std::ceil(maxPreDelayMs * 1.0e-3 * sampleRate)) + 4;
    for (auto& line : preDelayLines)
        line.prepare(maxPreDelay);

    plate.prepare(sampleRate, maxBlockSize);

    preDelaySamples.reset(sampleRate, preDelayGlideMs * 1.0e-3);
    const double ramp = smoothingMs * 1.0e-3;
    width.reset(sampleRate, ramp);
    lowCutHz.reset(sampleRate, ramp);
    highCutHz.reset(sampleRate, ramp);

    setParams(current);
    reset();
}

void ReverbEngine::reset() noexcept
{
    for (auto& line : preDelayLines)
        line.clear();
    for (auto& f : lowCut)
        f.reset();
    for (auto& f : highCut)
        f.reset();
    plate.reset();

    preDelaySamples.setCurrentAndTargetValue(preDelaySamples.getTargetValue());
    width.setCurrentAndTargetValue(width.getTargetValue());
    lowCutHz.setCurrentAndTargetValue(lowCutHz.getTargetValue());
    highCutHz.setCurrentAndTargetValue(highCutHz.getTargetValue());
    appliedLowCutHz = -1.0f;
    appliedHighCutHz = -1.0f;
    updateFilters(0);
}

void ReverbEngine::setParams(const Params& params) noexcept
{
    current = params;

    PlateReverb::Params p;
    p.decaySeconds = clampDecaySeconds(params.mode, params.decaySeconds);
    p.size = params.size;
    p.dampingHz = params.dampingHz;
    p.diffusion = params.diffusion;
    p.modRateHz = params.modRateHz;
    p.modDepth = params.modDepth;
    plate.setParams(p);

    preDelaySamples.setTargetValue(std::clamp(params.preDelayMs, 0.0f, maxPreDelayMs) * 1.0e-3f *
                                   static_cast<float>(fs));
    width.setTargetValue(std::clamp(params.width, 0.0f, 1.0f));
    lowCutHz.setTargetValue(std::max(1.0f, params.lowCutHz));
    highCutHz.setTargetValue(std::max(1.0f, params.highCutHz));

    setTailSeconds(
        tailSecondsFor(params.mode, params.decaySeconds, params.preDelayMs, params.size));
}

void ReverbEngine::updateFilters(int numSamples) noexcept
{
    const float lc = lowCutHz.getNextValue();
    const float hc = highCutHz.getNextValue();
    if (numSamples > 1)
    {
        lowCutHz.skip(numSamples - 1);
        highCutHz.skip(numSamples - 1);
    }
    if (std::abs(lc - appliedLowCutHz) > cutoffChangeThresholdHz)
    {
        for (auto& f : lowCut)
            f.setHighpass(fs, lc);
        appliedLowCutHz = lc;
    }
    if (std::abs(hc - appliedHighCutHz) > cutoffChangeThresholdHz)
    {
        for (auto& f : highCut)
            f.setLowpass(fs, hc);
        appliedHighCutHz = hc;
    }
}

float ReverbEngine::readPreDelay(const DelayLine<float>& line, float x,
                                 float delaySamples) const noexcept
{
    // Continuous from 0 (direct) through the integer reads up to the Hermite region, so a
    // pre-delay glide through zero never steps.
    if (delaySamples < 1.0f)
        return x + delaySamples * (line.read(1) - x);
    if (delaySamples < static_cast<float>(DelayLine<float>::minHermiteDelay))
        return line.read(1) + (delaySamples - 1.0f) * (line.read(2) - line.read(1));
    return line.readHermite(delaySamples);
}

void ReverbEngine::process(float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    updateFilters(numSamples);

    // Pre-delay (per channel, so a stereo algorithm in Phase 4 keeps its image).
    for (int i = 0; i < numSamples; ++i)
    {
        const float d = preDelaySamples.getNextValue();
        const float inL = left[i];
        const float inR = right[i];
        left[i] = readPreDelay(preDelayLines[0], inL, d);
        right[i] = readPreDelay(preDelayLines[1], inR, d);
        preDelayLines[0].write(inL);
        preDelayLines[1].write(inR);
    }

    plate.process(left, right, numSamples);

    // Return filters, then width as mid/side.
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = highCut[0].process(lowCut[0].process(left[i]));
        const float r = highCut[1].process(lowCut[1].process(right[i]));
        const float w = width.getNextValue();
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * w;
        left[i] = mid + side;
        right[i] = mid - side;
    }
}

} // namespace clearspace::dsp
