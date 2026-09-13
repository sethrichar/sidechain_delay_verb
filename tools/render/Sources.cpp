#include "Sources.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace clearspace::render
{

std::optional<SourceKind> parseSourceKind(const std::string& name)
{
    const auto lower = juce::String(name).toLowerCase();
    if (lower == "impulse")
        return SourceKind::impulse;
    if (lower == "sine")
        return SourceKind::sine;
    if (lower == "burst")
        return SourceKind::burst;
    if (lower == "noise")
        return SourceKind::noise;
    if (lower == "speechlike")
        return SourceKind::speechlike;
    return std::nullopt;
}

namespace
{

/** Paul Kellet's "refined" pink-noise filter (widely published, free to use). Output is
    roughly ±1 for white input in ±1; caller normalises. */
class PinkFilter
{
public:
    float process(float white)
    {
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;
        const auto pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;
        return pink * 0.11f;
    }

private:
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
};

void normaliseToPeak(juce::AudioBuffer<float>& buffer, float peak)
{
    const auto currentPeak = buffer.getMagnitude(0, buffer.getNumSamples());
    if (currentPeak > 0.0f)
        buffer.applyGain(peak / currentPeak);
}

} // namespace

juce::AudioBuffer<float> makeSource(const SourceSpec& spec, double sampleRate)
{
    const auto numSamples = static_cast<int>(std::llround(spec.seconds * sampleRate));
    const auto numChannels = std::max(1, spec.numChannels);
    juce::AudioBuffer<float> buffer(numChannels, std::max(1, numSamples));
    buffer.clear();

    const auto level = juce::Decibels::decibelsToGain(spec.levelDb);
    auto* out = buffer.getWritePointer(0);
    const auto n = buffer.getNumSamples();

    switch (spec.kind)
    {
    case SourceKind::impulse:
        out[0] = level;
        break;

    case SourceKind::sine:
    {
        const auto inc = juce::MathConstants<double>::twoPi * spec.freqHz / sampleRate;
        for (int i = 0; i < n; ++i)
            out[i] = level * static_cast<float>(std::sin(inc * i));
        break;
    }

    case SourceKind::burst:
    {
        const auto inc = juce::MathConstants<double>::twoPi * spec.freqHz / sampleRate;
        const auto onLen = static_cast<int>(std::llround(spec.burstOnSeconds * sampleRate));
        const auto period =
            onLen + static_cast<int>(std::llround(spec.burstOffSeconds * sampleRate));
        for (int i = 0; i < n; ++i)
        {
            const bool on = period <= 0 || (i % period) < onLen;
            out[i] = on ? level * static_cast<float>(std::sin(inc * i)) : 0.0f;
        }
        break;
    }

    case SourceKind::noise:
    {
        std::mt19937 rng(spec.seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (int i = 0; i < n; ++i)
            out[i] = level * dist(rng);
        break;
    }

    case SourceKind::speechlike:
    {
        std::mt19937 rng(spec.seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        PinkFilter pink;
        const auto inc = juce::MathConstants<double>::twoPi * spec.amRateHz / sampleRate;
        for (int i = 0; i < n; ++i)
        {
            const auto am = 0.5f + 0.5f * static_cast<float>(std::sin(inc * i));
            out[i] = pink.process(dist(rng)) * am;
        }
        normaliseToPeak(buffer, level);
        break;
    }
    }

    for (int ch = 1; ch < numChannels; ++ch)
        buffer.copyFrom(ch, 0, buffer, 0, 0, n);

    return buffer;
}

} // namespace clearspace::render
