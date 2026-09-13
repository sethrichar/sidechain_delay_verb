#pragma once

// Synthetic test sources for the offline renderer (CLAUDE.md §6). All deterministic.

#include <juce_audio_basics/juce_audio_basics.h>

#include <optional>
#include <string>

namespace clearspace::render
{

enum class SourceKind
{
    impulse,   // single full-scale-at-level sample at t = 0
    sine,      // steady sine at freqHz
    burst,     // sine gated: burstOnSeconds on / burstOffSeconds off, repeating
    noise,     // white noise, uniform, seeded
    speechlike // pink noise amplitude-modulated at ~4 Hz (syllable rate)
};

struct SourceSpec
{
    SourceKind kind = SourceKind::sine;
    double seconds = 2.0;
    double freqHz = 1000.0;
    float levelDb = -6.0f; // peak level of the generated signal
    unsigned seed = 1;
    double burstOnSeconds = 0.5;
    double burstOffSeconds = 0.5;
    double amRateHz = 4.0;
    int numChannels = 2; // channels are identical copies
};

/** Parses "impulse" | "sine" | "burst" | "noise" | "speechlike" (case-insensitive). */
std::optional<SourceKind> parseSourceKind(const std::string& name);

/** Generates the source. Channels are identical. */
juce::AudioBuffer<float> makeSource(const SourceSpec& spec, double sampleRate);

} // namespace clearspace::render
