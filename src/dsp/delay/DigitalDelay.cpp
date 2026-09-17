#include "DigitalDelay.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace clearspace::dsp
{

namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;
/** Filter coefficients are only recomputed when a cutoff has moved by more than this. */
constexpr float cutoffChangeThresholdHz = 1.0e-3f;
} // namespace

float DigitalDelay::timeToSamples(float ms) const noexcept
{
    // Nominally integer delays (500 ms at 96 kHz, 300 ms at 48 kHz, …) arrive a hair off the
    // integer: the APVTS log range returns 499.99997 for "500" (up to ~0.1 sample off at
    // 2000 ms / 96 kHz). A fractional part that small is inaudible but makes the Hermite kernel
    // add a −70 dB pre-ring sample, so anything within `integerSnapSamples` is snapped and read
    // as an exact integer (bit-exact echo).
    double samples = static_cast<double>(std::clamp(ms, minTimeMs, maxTimeMs)) * fs / 1000.0;
    const double nearest = std::round(samples);
    if (std::abs(samples - nearest) < integerSnapSamples)
        samples = nearest;
    const auto lo = static_cast<float>(DelayLine<float>::minHermiteDelay);
    return std::clamp(static_cast<float>(samples), lo,
                      std::max(lo, static_cast<float>(maxDelaySamples)));
}

double DigitalDelay::tailSecondsFor(float timeMs, float feedback) noexcept
{
    const double time = (std::clamp(timeMs, minTimeMs, maxTimeMs) + maxModMs) * 1.0e-3;
    const auto fb = std::clamp(feedback, 0.0f, maxFeedback);
    if (fb <= 0.0f)
        return time;
    const double repeats = std::ceil(std::log(1.0e-3) / std::log(static_cast<double>(fb)));
    return time * (1.0 + std::max(0.0, repeats));
}

void DigitalDelay::prepare(double sampleRate, int)
{
    fs = sampleRate;
    maxDelaySamples = static_cast<int>(std::ceil((maxTimeMs + maxModMs) * 1.0e-3 * sampleRate)) + 4;
    for (auto& ch : channels)
        ch.line.prepare(maxDelaySamples);

    crossfadeLength = std::max(1, static_cast<int>(std::lround(crossfadeMs * 1.0e-3 * sampleRate)));
    const double rot = 0.5 * std::numbers::pi / crossfadeLength;
    fadeRotCos = static_cast<float>(std::cos(rot));
    fadeRotSin = static_cast<float>(std::sin(rot));

    const double ramp = smoothingMs * 1.0e-3;
    feedbackGain.reset(sampleRate, ramp);
    modDepthSamples.reset(sampleRate, ramp);
    pingPongBlend.reset(sampleRate, ramp);
    lowCutHz.reset(sampleRate, ramp);
    highCutHz.reset(sampleRate, ramp);

    setParams(current);
    reset();
}

void DigitalDelay::reset() noexcept
{
    for (auto& ch : channels)
    {
        ch.line.clear();
        ch.lowCut.reset();
        ch.highCut.reset();
    }

    // Snap: both heads on the requested time, no crossfade in flight, smoothers at target.
    headDelaySamples = {requestedDelaySamples, requestedDelaySamples};
    activeHead = 0;
    crossfading = false;
    crossfadeRemaining = 0;
    fadeCos = 1.0f;
    fadeSin = 0.0f;
    lfoPhase = 0.0;

    for (auto* s : {&feedbackGain, &modDepthSamples, &pingPongBlend})
        s->setCurrentAndTargetValue(s->getTargetValue());
    lowCutHz.setCurrentAndTargetValue(lowCutHz.getTargetValue());
    highCutHz.setCurrentAndTargetValue(highCutHz.getTargetValue());
    appliedLowCutHz = -1.0f;
    appliedHighCutHz = -1.0f;
    updateFilters(0);
}

void DigitalDelay::setParams(const Params& params) noexcept
{
    current = params;
    requestedDelaySamples = timeToSamples(params.timeMs);
    feedbackGain.setTargetValue(std::clamp(params.feedback, 0.0f, maxFeedback));
    modDepthSamples.setTargetValue(std::clamp(params.modDepth, 0.0f, 1.0f) * maxModMs * 1.0e-3f *
                                   static_cast<float>(fs));
    lfoIncrement = twoPi * std::clamp(static_cast<double>(params.modRateHz), 0.0, 0.25 * fs) / fs;
    pingPongBlend.setTargetValue(params.pingPong ? 1.0f : 0.0f);
    lowCutHz.setTargetValue(std::max(1.0f, params.lowCutHz));
    highCutHz.setTargetValue(std::max(1.0f, params.highCutHz));
    setTailSeconds(tailSecondsFor(params.timeMs, params.feedback));
}

void DigitalDelay::updateFilters(int numSamples) noexcept
{
    // Cutoffs ramp over 20 ms but the coefficients are recomputed once per block: cheap, and
    // at any practical block size the steps are inaudible.
    const float lc = lowCutHz.getNextValue();
    const float hc = highCutHz.getNextValue();
    if (numSamples > 1)
    {
        lowCutHz.skip(numSamples - 1);
        highCutHz.skip(numSamples - 1);
    }
    if (std::abs(lc - appliedLowCutHz) > cutoffChangeThresholdHz)
    {
        for (auto& ch : channels)
            ch.lowCut.setHighpass(fs, lc);
        appliedLowCutHz = lc;
    }
    if (std::abs(hc - appliedHighCutHz) > cutoffChangeThresholdHz)
    {
        for (auto& ch : channels)
            ch.highCut.setLowpass(fs, hc);
        appliedHighCutHz = hc;
    }
}

void DigitalDelay::maybeStartCrossfade() noexcept
{
    if (crossfading)
        return;
    if (std::abs(requestedDelaySamples - headDelaySamples[static_cast<size_t>(activeHead)]) <=
        retargetThresholdSamples)
        return;
    headDelaySamples[static_cast<size_t>(1 - activeHead)] = requestedDelaySamples;
    crossfading = true;
    crossfadeRemaining = crossfadeLength;
    fadeCos = 1.0f;
    fadeSin = 0.0f;
}

void DigitalDelay::process(float* left, float* right, int numSamples) noexcept
{
    if (maxDelaySamples <= 0 || numSamples <= 0)
        return;

    updateFilters(numSamples);
    maybeStartCrossfade();

    auto& L = channels[0];
    auto& R = channels[1];

    for (int i = 0; i < numSamples; ++i)
    {
        // LFO: left on sine, right on cosine (quadrature), depth in samples.
        const float depth = modDepthSamples.getNextValue();
        const float offsetL = depth * static_cast<float>(std::sin(lfoPhase));
        const float offsetR = depth * static_cast<float>(std::cos(lfoPhase));
        lfoPhase += lfoIncrement;
        if (lfoPhase >= twoPi)
            lfoPhase -= twoPi;

        // Read heads.
        const size_t a = static_cast<size_t>(activeHead);
        const size_t b = 1 - a;
        float yL = L.line.readHermite(headDelaySamples[a] + offsetL);
        float yR = R.line.readHermite(headDelaySamples[a] + offsetR);
        if (crossfading)
        {
            yL = yL * fadeCos + L.line.readHermite(headDelaySamples[b] + offsetL) * fadeSin;
            yR = yR * fadeCos + R.line.readHermite(headDelaySamples[b] + offsetR) * fadeSin;
            const float c = fadeCos * fadeRotCos - fadeSin * fadeRotSin;
            const float s = fadeSin * fadeRotCos + fadeCos * fadeRotSin;
            fadeCos = c;
            fadeSin = s;
            if (--crossfadeRemaining <= 0)
            {
                activeHead = static_cast<int>(b);
                crossfading = false;
                fadeCos = 1.0f;
                fadeSin = 0.0f;
            }
        }

        // Feedback path: tone filters, then the clamped loop gain.
        const float fb = feedbackGain.getNextValue();
        const float fbL = L.highCut.process(L.lowCut.process(yL)) * fb;
        const float fbR = R.highCut.process(R.lowCut.process(yR)) * fb;

        // Stereo ↔ ping-pong blend (0 = independent L/R, 1 = mono in → L, cross-fed).
        const float pp = pingPongBlend.getNextValue();
        const float inL = left[i];
        const float inR = right[i];
        const float mono = 0.5f * (inL + inR);
        const float writeL = inL + (mono - inL) * pp + fbL + (fbR - fbL) * pp;
        const float writeR = inR - inR * pp + fbR + (fbL - fbR) * pp;
        L.line.write(writeL);
        R.line.write(writeR);

        left[i] = yL;
        right[i] = yR;
    }
}

} // namespace clearspace::dsp
