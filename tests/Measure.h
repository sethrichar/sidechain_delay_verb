#pragma once

// Measurement helpers shared by the DSP tests (CLAUDE.md §6): windowed RMS/peak, crossing
// times, Schroeder backward-integration RT60. Header-only.

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace clearspace::test
{

inline float linearToDb(float x)
{
    return 20.0f * std::log10(std::max(x, 1.0e-10f));
}

inline int toSample(double seconds, double sampleRate)
{
    return static_cast<int>(std::lround(seconds * sampleRate));
}

/** RMS (linear) of one channel between two sample indices [start, end). */
inline float rmsIn(const juce::AudioBuffer<float>& b, int channel, int start, int end)
{
    start = std::clamp(start, 0, b.getNumSamples());
    end = std::clamp(end, start, b.getNumSamples());
    if (end <= start)
        return 0.0f;
    double acc = 0.0;
    const float* p = b.getReadPointer(channel);
    for (int i = start; i < end; ++i)
        acc += static_cast<double>(p[i]) * p[i];
    return static_cast<float>(std::sqrt(acc / (end - start)));
}

inline float rmsDbIn(const juce::AudioBuffer<float>& b, int channel, double startSeconds,
                     double endSeconds, double sampleRate)
{
    return linearToDb(
        rmsIn(b, channel, toSample(startSeconds, sampleRate), toSample(endSeconds, sampleRate)));
}

/** Peak |x| of one channel between two sample indices [start, end). */
inline float peakIn(const juce::AudioBuffer<float>& b, int channel, int start, int end)
{
    start = std::clamp(start, 0, b.getNumSamples());
    end = std::clamp(end, start, b.getNumSamples());
    if (end <= start)
        return 0.0f;
    return b.getMagnitude(channel, start, end - start);
}

inline float peakDbIn(const juce::AudioBuffer<float>& b, int channel, double startSeconds,
                      double endSeconds, double sampleRate)
{
    return linearToDb(
        peakIn(b, channel, toSample(startSeconds, sampleRate), toSample(endSeconds, sampleRate)));
}

/** First index in [start, end) where |x| > threshold, or -1. */
inline int firstAbove(const float* data, int start, int end, float threshold)
{
    for (int i = std::max(0, start); i < end; ++i)
        if (std::abs(data[i]) > threshold)
            return i;
    return -1;
}

/** First index in [start, end) where x >= threshold (for monotone-ish sequences), or -1. */
inline int firstAtLeast(const std::vector<float>& v, int start, float threshold)
{
    for (int i = std::max(0, start); i < static_cast<int>(v.size()); ++i)
        if (v[static_cast<size_t>(i)] >= threshold)
            return i;
    return -1;
}

/** First index in [start, end) where x <= threshold, or -1. */
inline int firstAtMost(const std::vector<float>& v, int start, float threshold)
{
    for (int i = std::max(0, start); i < static_cast<int>(v.size()); ++i)
        if (v[static_cast<size_t>(i)] <= threshold)
            return i;
    return -1;
}

/** Largest |x[i] - x[i-1]| over [start, end). */
inline float maxSampleStep(const float* data, int start, int end)
{
    float m = 0.0f;
    for (int i = std::max(1, start); i < end; ++i)
        m = std::max(m, std::abs(data[i] - data[i - 1]));
    return m;
}

/** Any NaN or Inf anywhere in the buffer. */
inline bool hasNonFinite(const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* p = b.getReadPointer(ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (!std::isfinite(p[i]))
                return true;
    }
    return false;
}

/** RT60 in seconds from an impulse response via Schroeder backward integration: the
    energy-decay curve is fitted (least squares, in dB) between `fitFromDb` and `fitToDb`
    below its start and extrapolated to −60 dB. Returns 0 if the response is too short. */
inline double schroederRT60(const float* ir, int numSamples, double sampleRate,
                            double fitFromDb = -5.0, double fitToDb = -25.0)
{
    if (numSamples < 2)
        return 0.0;

    std::vector<double> edc(static_cast<size_t>(numSamples));
    double acc = 0.0;
    for (int i = numSamples - 1; i >= 0; --i)
    {
        acc += static_cast<double>(ir[i]) * ir[i];
        edc[static_cast<size_t>(i)] = acc;
    }
    if (acc <= 0.0)
        return 0.0;

    const double refDb = 10.0 * std::log10(acc);
    // Linear regression of dB vs time over the fit range.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        const double e = edc[static_cast<size_t>(i)];
        if (e <= 0.0)
            break;
        const double db = 10.0 * std::log10(e) - refDb;
        if (db > fitFromDb)
            continue;
        if (db < fitToDb)
            break;
        const double t = i / sampleRate;
        sx += t;
        sy += db;
        sxx += t * t;
        sxy += t * db;
        ++n;
    }
    if (n < 2)
        return 0.0;
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx); // dB per second (< 0)
    if (!(slope < 0.0))
        return 0.0;
    return -60.0 / slope;
}

} // namespace clearspace::test
