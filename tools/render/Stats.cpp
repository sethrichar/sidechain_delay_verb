#include "Stats.h"

#include <cmath>
#include <limits>
#include <sstream>

namespace clearspace::render
{

long Stats::totalNanCount() const
{
    long total = 0;
    for (const auto& ch : channels)
        total += ch.nanCount;
    return total;
}

long Stats::totalInfCount() const
{
    long total = 0;
    for (const auto& ch : channels)
        total += ch.infCount;
    return total;
}

namespace
{
float toDb(float linear)
{
    return linear > 0.0f ? std::max(-200.0f, juce::Decibels::gainToDecibels(linear, -200.0f))
                         : -200.0f;
}
} // namespace

Stats computeStats(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    Stats stats;
    stats.sampleRate = sampleRate;
    stats.numChannels = buffer.getNumChannels();
    stats.numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < stats.numChannels; ++ch)
    {
        ChannelStats cs;
        const auto* data = buffer.getReadPointer(ch);
        double sumSquares = 0.0;
        long finiteCount = 0;

        for (int i = 0; i < stats.numSamples; ++i)
        {
            const auto x = data[i];
            if (std::isnan(x))
            {
                ++cs.nanCount;
                continue;
            }
            if (std::isinf(x))
            {
                ++cs.infCount;
                continue;
            }
            const auto cls = std::fpclassify(x);
            if (cls == FP_SUBNORMAL)
                ++cs.denormalCount;
            if (cls != FP_ZERO && cs.firstNonZero < 0)
                cs.firstNonZero = i;

            const auto mag = std::abs(x);
            cs.peak = std::max(cs.peak, mag);
            sumSquares += static_cast<double>(x) * static_cast<double>(x);
            ++finiteCount;
        }

        cs.rms = finiteCount > 0
                     ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(finiteCount)))
                     : 0.0f;
        cs.peakDb = toDb(cs.peak);
        cs.rmsDb = toDb(cs.rms);
        stats.channels.push_back(cs);
    }

    return stats;
}

std::string formatStats(const Stats& stats)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(6);
    out << "sampleRate=" << stats.sampleRate << '\n';
    out << "channels=" << stats.numChannels << '\n';
    out << "samples=" << stats.numSamples << '\n';
    out << "nanCount=" << stats.totalNanCount() << '\n';
    out << "infCount=" << stats.totalInfCount() << '\n';

    for (size_t ch = 0; ch < stats.channels.size(); ++ch)
    {
        const auto& cs = stats.channels[ch];
        const std::string p = "ch" + std::to_string(ch) + ".";
        out << p << "peak=" << cs.peak << '\n';
        out << p << "peakDb=" << cs.peakDb << '\n';
        out << p << "rms=" << cs.rms << '\n';
        out << p << "rmsDb=" << cs.rmsDb << '\n';
        out << p << "nanCount=" << cs.nanCount << '\n';
        out << p << "infCount=" << cs.infCount << '\n';
        out << p << "denormalCount=" << cs.denormalCount << '\n';
        out << p << "firstNonZero=" << cs.firstNonZero << '\n';
    }
    return out.str();
}

} // namespace clearspace::render
