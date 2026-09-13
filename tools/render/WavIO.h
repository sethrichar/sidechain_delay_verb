#pragma once

// WAV read/write through juce::WavAudioFormat. Output is always 32-bit float so
// round trips are bit-exact.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <optional>

namespace clearspace::render
{

struct WavData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
};

/** Returns nullopt if the file can't be opened or isn't a readable WAV. */
std::optional<WavData> readWav(const juce::File& file);

/** Writes 32-bit float WAV. Returns false on failure. Overwrites. */
bool writeWav(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate);

} // namespace clearspace::render
