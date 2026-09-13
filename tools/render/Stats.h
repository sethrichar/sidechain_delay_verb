#pragma once

// Measurements over a rendered buffer (CLAUDE.md §6: NaN/Inf/denormal checks, levels).

#include <juce_audio_basics/juce_audio_basics.h>

#include <string>
#include <vector>

namespace clearspace::render
{

struct ChannelStats
{
    float peak = 0.0f;      // linear
    float peakDb = -200.0f; // dBFS (floored at -200)
    float rms = 0.0f;       // linear
    float rmsDb = -200.0f;  // dBFS
    long nanCount = 0;
    long infCount = 0;
    long denormalCount = 0;
    long firstNonZero = -1; // sample index, -1 if the channel is all-zero
};

struct Stats
{
    double sampleRate = 0.0;
    int numChannels = 0;
    int numSamples = 0;
    std::vector<ChannelStats> channels;

    long totalNanCount() const;
    long totalInfCount() const;
    bool isClean() const { return totalNanCount() == 0 && totalInfCount() == 0; }
};

Stats computeStats(const juce::AudioBuffer<float>& buffer, double sampleRate);

/** One "key=value" per line, grep-able: sampleRate=, channels=, samples=, ch0.peak=, … */
std::string formatStats(const Stats& stats);

} // namespace clearspace::render
