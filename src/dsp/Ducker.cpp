#include "Ducker.h"

#include "Util.h"

#include <algorithm>
#include <cmath>

namespace clearspace::dsp
{

namespace
{
constexpr float minLevelDb = -120.0f;
constexpr float dbToLinearScale = -0.11512925464970229f; // −ln(10)/20
constexpr float minMeanSquare = 1.0e-12f;                // −120 dB floor before the log

/** One-pole coefficient for a move that is 95 % complete after `timeMs`. */
float ballisticCoef(float timeMs, double sampleRate)
{
    const double samples = std::max(1.0e-3, static_cast<double>(timeMs)) * 1.0e-3 * sampleRate;
    return static_cast<float>(1.0 - std::exp(-3.0 / samples));
}
} // namespace

void Ducker::prepare(double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    gains.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 1.0f);
    detectorCoef =
        static_cast<float>(1.0 - std::exp(-1.0 / (detectorWindowMs * 1.0e-3 * sampleRate)));
    lastKeyHpfHz = -1.0f; // force a filter redesign for the new rate
    updateCoefficients();
    reset();
}

void Ducker::reset()
{
    keyHpf.reset();
    meanSquare = 0.0f;
    currentGrDb = 0.0f;
    holdCounter = 0;
    std::fill(gains.begin(), gains.end(), 1.0f);
}

void Ducker::setParams(const DuckerParams& newParams)
{
    params = newParams;
    updateCoefficients();
}

void Ducker::updateCoefficients()
{
    attackCoef = ballisticCoef(params.attackMs, sampleRate);
    releaseCoef = ballisticCoef(params.releaseMs, sampleRate);
    holdSamples =
        static_cast<int>(std::lround(std::max(0.0f, params.holdMs) * 1.0e-3 * sampleRate));

    if (!exactlyEqual(params.keyHpfHz, lastKeyHpfHz) || !exactlyEqual(sampleRate, lastSampleRate))
    {
        keyHpf.set(Biquad::Type::highPass, sampleRate, params.keyHpfHz);
        lastKeyHpfHz = params.keyHpfHz;
        lastSampleRate = sampleRate;
    }
}

float Ducker::computeGainReductionDb(float levelDb, float thresholdDb, float depthDb)
{
    // Giannoulis et al. eq. (4): soft-knee static curve, expressed as reduction (≥ 0).
    const float over = levelDb - thresholdDb;
    float reduction = 0.0f;

    if (2.0f * over < -kneeWidthDb)
        reduction = 0.0f;
    else if (2.0f * std::abs(over) <= kneeWidthDb)
    {
        const float t = over + kneeWidthDb * 0.5f;
        reduction = (1.0f - 1.0f / ratio) * t * t / (2.0f * kneeWidthDb);
    }
    else
        reduction = over * (1.0f - 1.0f / ratio);

    return std::clamp(reduction, 0.0f, std::max(0.0f, depthDb));
}

void Ducker::computeGains(const float* key, int n)
{
    n = std::min(n, static_cast<int>(gains.size()));
    const bool active = params.enabled && params.depthDb > 0.0f;

    for (int i = 0; i < n; ++i)
    {
        // Detector: HPF → mean square (one-pole) → dB, calibrated so a sine reads its peak.
        const float x = keyHpf.process(key[i]);
        meanSquare += detectorCoef * (x * x - meanSquare);
        const float levelDb =
            std::max(minLevelDb, 10.0f * std::log10(std::max(minMeanSquare, 2.0f * meanSquare)));

        const float target =
            active ? computeGainReductionDb(levelDb, params.thresholdDb, params.depthDb) : 0.0f;

        // Ballistics in the dB domain. More reduction requested → attack and re-arm hold;
        // less → hold the current value, then release.
        if (target >= currentGrDb)
        {
            currentGrDb += attackCoef * (target - currentGrDb);
            holdCounter = holdSamples;
        }
        else if (holdCounter > 0)
        {
            --holdCounter;
        }
        else
        {
            currentGrDb += releaseCoef * (target - currentGrDb);
        }

        gains[static_cast<size_t>(i)] = std::exp(currentGrDb * dbToLinearScale);
    }
}

void Ducker::process(const float* key, float* const* wet, int numChannels, int n)
{
    computeGains(key, n);
    n = std::min(n, static_cast<int>(gains.size()));
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* w = wet[ch];
        for (int i = 0; i < n; ++i)
            w[i] *= gains[static_cast<size_t>(i)];
    }
}

} // namespace clearspace::dsp
