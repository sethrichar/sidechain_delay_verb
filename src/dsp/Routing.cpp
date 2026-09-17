#include "Routing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace clearspace::dsp
{

void Routing::mixGains(float mixPercent, float& dryGain, float& wetGain) noexcept
{
    const float m = std::clamp(mixPercent, 0.0f, 100.0f) / 100.0f;
    const float angle = m * static_cast<float>(std::numbers::pi) * 0.5f;
    dryGain = std::cos(angle);
    wetGain = std::sin(angle);
    // Exact end points so mix = 0 is a bit-exact dry path and mix = 100 is pure wet.
    if (m <= 0.0f)
    {
        dryGain = 1.0f;
        wetGain = 0.0f;
    }
    else if (m >= 1.0f)
    {
        dryGain = 0.0f;
        wetGain = 1.0f;
    }
}

float Routing::levelToGain(float dB) noexcept
{
    if (dB <= levelOffDb)
        return 0.0f;
    return juce::Decibels::decibelsToGain(dB);
}

void Routing::prepare(double sampleRate, int maxBlockSize)
{
    maxBlock = std::max(1, maxBlockSize);
    dryBuf.setSize(2, maxBlock);
    delayBuf.setSize(2, maxBlock);
    reverbBuf.setSize(2, maxBlock);

    const double rampSeconds = smoothingMs * 1.0e-3;
    for (auto* s : {&dryGain, &wetGain, &outputGain, &delaySection.level, &delaySection.bypassGain,
                    &reverbSection.level, &reverbSection.bypassGain})
        s->reset(sampleRate, rampSeconds);

    setParams(current);
    reset();
}

void Routing::reset() noexcept
{
    dryBuf.clear();
    delayBuf.clear();
    reverbBuf.clear();
    for (auto* s : {&dryGain, &wetGain, &outputGain, &delaySection.level, &delaySection.bypassGain,
                    &reverbSection.level, &reverbSection.bypassGain})
        s->setCurrentAndTargetValue(s->getTargetValue());
    delaySection.wasActive = delaySection.isActive();
    reverbSection.wasActive = reverbSection.isActive();
}

void Routing::setParams(const Params& params) noexcept
{
    current = params;

    float dry = 1.0f, wet = 0.0f;
    mixGains(params.mixPercent, dry, wet);
    dryGain.setTargetValue(dry);
    wetGain.setTargetValue(wet);
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(params.outputTrimDb));

    delaySection.level.setTargetValue(levelToGain(params.delayLevelDb));
    delaySection.bypassGain.setTargetValue(params.delayBypass ? 0.0f : 1.0f);
    reverbSection.level.setTargetValue(levelToGain(params.reverbLevelDb));
    reverbSection.bypassGain.setTargetValue(params.reverbBypass ? 0.0f : 1.0f);
}

void Routing::runSection(Section& section, Effect& effect, const float* duckGain, float* left,
                         float* right, int numSamples) noexcept
{
    const bool active = section.isActive();

    if (!active)
    {
        if (section.wasActive)
            effect.reset(); // fade-out finished: drop the tail so re-enabling starts clean
        section.wasActive = false;
        juce::FloatVectorOperations::clear(left, numSamples);
        juce::FloatVectorOperations::clear(right, numSamples);
        // Keep the level smoother in step even while inactive.
        section.level.skip(numSamples);
        return;
    }

    section.wasActive = true;
    effect.process(left, right, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g =
            section.level.getNextValue() * section.bypassGain.getNextValue() * duckGain[i];
        left[i] *= g;
        right[i] *= g;
    }
}

void Routing::process(const float* inL, const float* inR, const float* delayGain,
                      const float* reverbGain, Effect& delay, Effect& reverb, float* outL,
                      float* outR, int numSamples) noexcept
{
    numSamples = std::min(numSamples, maxBlock);
    if (numSamples <= 0)
        return;

    auto* dryL = dryBuf.getWritePointer(0);
    auto* dryR = dryBuf.getWritePointer(1);
    auto* dL = delayBuf.getWritePointer(0);
    auto* dR = delayBuf.getWritePointer(1);
    auto* rL = reverbBuf.getWritePointer(0);
    auto* rR = reverbBuf.getWritePointer(1);

    // Split: dry copy and the delay feed (out may alias in, so copy first).
    juce::FloatVectorOperations::copy(dryL, inL, numSamples);
    juce::FloatVectorOperations::copy(dryR, inR, numSamples);
    juce::FloatVectorOperations::copy(dL, inL, numSamples);
    juce::FloatVectorOperations::copy(dR, inR, numSamples);

    runSection(delaySection, delay, delayGain, dL, dR, numSamples);

    // Reverb feed: serial adds the (ducked, levelled) delay return to the dry input.
    juce::FloatVectorOperations::copy(rL, dryL, numSamples);
    juce::FloatVectorOperations::copy(rR, dryR, numSamples);
    if (current.serial)
    {
        juce::FloatVectorOperations::add(rL, dL, numSamples);
        juce::FloatVectorOperations::add(rR, dR, numSamples);
    }

    runSection(reverbSection, reverb, reverbGain, rL, rR, numSamples);

    // Equal-power mix and output trim.
    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = dryGain.getNextValue();
        const float wet = wetGain.getNextValue();
        const float trim = outputGain.getNextValue();
        outL[i] = (dryL[i] * dry + (dL[i] + rL[i]) * wet) * trim;
        outR[i] = (dryR[i] * dry + (dR[i] + rR[i]) * wet) * trim;
    }
}

double Routing::getTailSeconds(const Effect& delay, const Effect& reverb) const noexcept
{
    const double delayTail = current.delayBypass ? 0.0 : delay.getTailSeconds();
    const double reverbTail = current.reverbBypass ? 0.0 : reverb.getTailSeconds();
    return current.serial ? delayTail + reverbTail : std::max(delayTail, reverbTail);
}

} // namespace clearspace::dsp
