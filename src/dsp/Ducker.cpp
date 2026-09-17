#include "Ducker.h"

#include <algorithm>
#include <cmath>

namespace clearspace::dsp
{

namespace
{
constexpr float dbToLinearScale = -0.11512925f; // -ln(10)/20, so gain = exp(gr * scale)
constexpr float minMeanSquare = 1.0e-20f;       // ≈ -200 dBFS floor for the log
} // namespace

float Ducker::coefficientFor95PercentIn(float ms, double sampleRate) noexcept
{
    // 95 % of a one-pole step is reached after ~3 time constants.
    const double tauSamples = std::max(1.0e-9, static_cast<double>(ms)) * 1.0e-3 * sampleRate / 3.0;
    return static_cast<float>(1.0 - std::exp(-1.0 / tauSamples));
}

float Ducker::computeGainReductionDb(float levelDb, float thresholdDb, float depthDb) noexcept
{
    // Giannoulis et al. eq. (4), soft knee of width kneeDb.
    const float over = levelDb - thresholdDb;
    float gr = 0.0f;
    if (2.0f * over <= -kneeDb)
        gr = 0.0f;
    else if (2.0f * over >= kneeDb)
        gr = over * (1.0f - 1.0f / ratio);
    else
    {
        const float t = over + kneeDb * 0.5f;
        gr = (1.0f - 1.0f / ratio) * t * t / (2.0f * kneeDb);
    }
    return std::clamp(gr, 0.0f, std::max(0.0f, depthDb));
}

void Ducker::prepare(double sampleRate, int)
{
    fs = sampleRate;
    rmsCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (rmsWindowMs * 1.0e-3 * sampleRate)));
    disableCoeff = coefficientFor95PercentIn(disableFadeMs, sampleRate);
    lastHpfHz = -1.0f;
    setParams(current);
    reset();
}

void Ducker::reset() noexcept
{
    keyHpf.reset();
    meanSquare = 0.0f;
    envDb = 0.0f;
    blockPeakDb = 0.0f;
    holdCounter = 0;
}

void Ducker::setParams(const Params& params) noexcept
{
    current = params;
    attackCoeff = coefficientFor95PercentIn(params.attackMs, fs);
    releaseCoeff = coefficientFor95PercentIn(params.releaseMs, fs);
    holdSamples = static_cast<int>(std::lround(std::max(0.0f, params.holdMs) * 1.0e-3 * fs));

    if (std::abs(params.keyHpfHz - lastHpfHz) > 1.0e-3f)
    {
        keyHpf.setHighpass(fs, params.keyHpfHz);
        lastHpfHz = params.keyHpfHz;
    }
}

void Ducker::process(const float* key, float* gainOut, int numSamples) noexcept
{
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Key HPF.   2. RMS detector → dB.
        const float k = keyHpf.process(key[i]);
        meanSquare += rmsCoeff * (k * k - meanSquare);
        const float levelDb = 10.0f * std::log10(meanSquare + minMeanSquare);

        // 3. Gain computer (soft knee, 20:1, clamped to depth).
        const float target =
            current.enabled ? computeGainReductionDb(levelDb, current.thresholdDb, current.depthDb)
                            : 0.0f;

        // 4. Ballistics: attack / hold / release on the gain reduction in dB.
        if (!current.enabled)
        {
            envDb += disableCoeff * (target - envDb);
            holdCounter = 0;
        }
        else if (target > envDb)
        {
            envDb += attackCoeff * (target - envDb);
            holdCounter = holdSamples; // any (re)trigger re-arms the hold
        }
        else if (holdCounter > 0)
        {
            --holdCounter; // hold: gain stays where it is
        }
        else
        {
            envDb += releaseCoeff * (target - envDb);
        }

        if (envDb < 1.0e-4f)
            envDb = 0.0f; // snap to unity so silence is exactly silent

        peak = std::max(peak, envDb);

        // 5. Linear gain for the wet return.
        gainOut[i] = std::exp(envDb * dbToLinearScale);
    }

    blockPeakDb = peak;
}

} // namespace clearspace::dsp
