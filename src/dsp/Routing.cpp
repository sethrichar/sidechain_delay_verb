#include "Routing.h"

#include "Util.h"

#include <algorithm>
#include <cmath>

namespace clearspace::dsp
{

namespace
{
constexpr float halfPi = 1.57079632679489661923f;
}

float Routing::dbToGain(float db)
{
    return db <= -100.0f ? 0.0f : std::pow(10.0f, db * 0.05f);
}

void Routing::equalPowerGains(float mix01, float& dryGainOut, float& wetGainOut)
{
    const float m = std::clamp(mix01, 0.0f, 1.0f);
    dryGainOut = std::cos(m * halfPi);
    wetGainOut = std::sin(m * halfPi);
    // Land exactly on 1 / 0 at the ends so a 0 % mix is a bit-exact pass-through.
    if (m <= 0.0f)
    {
        dryGainOut = 1.0f;
        wetGainOut = 0.0f;
    }
    else if (m >= 1.0f)
    {
        dryGainOut = 0.0f;
        wetGainOut = 1.0f;
    }
}

double Routing::delayTailSeconds(double delayMs, double feedbackPercent)
{
    const double time = std::max(0.0, delayMs) * 1.0e-3;
    const double fb = std::clamp(feedbackPercent * 0.01, 0.0, 0.99);
    double repeats = 1.0;
    if (fb > 0.0)
        repeats += std::floor(3.0 / -std::log10(fb)); // echoes still above −60 dB
    return std::min(maxTailSeconds, time * repeats);
}

double Routing::tailSeconds(double delayMs, double feedbackPercent, bool delayBypass,
                            double reverbDecaySeconds, bool reverbBypass)
{
    const double delayTail = delayBypass ? 0.0 : delayTailSeconds(delayMs, feedbackPercent);
    const double reverbTail =
        reverbBypass ? 0.0 : std::min(maxTailSeconds, std::max(0.0, reverbDecaySeconds));
    return std::max(delayTail, reverbTail);
}

void Routing::prepare(double sampleRate)
{
    for (auto* s : {&inputGain, &outputGain, &dryGain, &wetGain, &delayReturnGain,
                    &reverbReturnGain, &serialGain})
        s->prepare(sampleRate, smoothingMs);
}

void Routing::setParams(const RoutingParams& p)
{
    inputGain.setTarget(dbToGain(p.inputTrimDb));
    outputGain.setTarget(dbToGain(p.outputTrimDb));

    float dry = 1.0f, wet = 0.0f;
    equalPowerGains(p.mixPercent * 0.01f, dry, wet);
    dryGain.setTarget(dry);
    wetGain.setTarget(wet);

    delayReturnGain.setTarget(p.delayBypass ? 0.0f : dbToGain(p.delayLevelDb));
    reverbReturnGain.setTarget(p.reverbBypass ? 0.0f : dbToGain(p.reverbLevelDb));
    serialGain.setTarget(p.serial ? 1.0f : 0.0f);
}

void Routing::reset()
{
    for (auto* s : {&inputGain, &outputGain, &dryGain, &wetGain, &delayReturnGain,
                    &reverbReturnGain, &serialGain})
        s->snapToTarget();
}

void Routing::applyGain(LinearSmoother& gain, float* const* buffer, int numChannels, int n)
{
    if (!gain.isSmoothing())
    {
        const float g = gain.getCurrent();
        if (exactlyEqual(g, 1.0f))
            return;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
                buffer[ch][i] *= g;
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        const float g = gain.getNext();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer[ch][i] *= g;
    }
}

void Routing::applyInputTrim(float* const* buffer, int numChannels, int n)
{
    applyGain(inputGain, buffer, numChannels, n);
}

void Routing::applyDelayReturn(float* const* buffer, int numChannels, int n)
{
    applyGain(delayReturnGain, buffer, numChannels, n);
}

void Routing::applyReverbReturn(float* const* buffer, int numChannels, int n)
{
    applyGain(reverbReturnGain, buffer, numChannels, n);
}

void Routing::buildReverbInput(const float* const* dry, const float* const* delayWet,
                               float* const* reverbIn, int numChannels, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float g = serialGain.getNext();
        for (int ch = 0; ch < numChannels; ++ch)
            reverbIn[ch][i] = dry[ch][i] + g * delayWet[ch][i];
    }
}

void Routing::mixOutput(const float* const* dry, const float* const* delayWet,
                        const float* const* reverbWet, float* const* out, int numChannels, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float d = dryGain.getNext();
        const float w = wetGain.getNext();
        const float o = outputGain.getNext();
        for (int ch = 0; ch < numChannels; ++ch)
            out[ch][i] = (dry[ch][i] * d + (delayWet[ch][i] + reverbWet[ch][i]) * w) * o;
    }
}

} // namespace clearspace::dsp
