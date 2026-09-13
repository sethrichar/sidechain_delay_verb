#pragma once

// Drives ClearSpaceProcessor offline: prepare → chunked processBlock → output buffer.

#include "PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace clearspace::render
{

struct RenderSettings
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    /** "id=value" pairs. Value is a number in the parameter's natural units, or for
        choice/bool parameters a choice name / "on" / "off". */
    std::vector<std::pair<std::string, std::string>> parameterOverrides;
    /** Append getTailLengthSeconds() of silence after the input. */
    bool appendTail = false;
    /** Use the mono→stereo bus layout (main input bus = mono, fed from input channel 0). */
    bool monoInput = false;
    /** When non-empty, the sidechain bus is enabled (mono or stereo to match the channel
        count) and fed from this buffer; samples past its end are silent. */
    juce::AudioBuffer<float> sidechain;
    /** Called before every processBlock with the block index and its first sample position,
        so tests can automate parameters mid-render or sample the meters. */
    std::function<void(ClearSpaceProcessor&, int blockIndex, int startSample)> perBlockHook;

    bool hasSidechain() const
    {
        return sidechain.getNumChannels() > 0 && sidechain.getNumSamples() > 0;
    }
};

struct RenderError
{
    std::string message;
};

struct RenderOutput
{
    juce::AudioBuffer<float> buffer; // always stereo
    std::vector<RenderError> errors; // non-empty means the render did not run
    bool ok() const { return errors.empty(); }

    /** Ducker meters sampled after every block (positive dB), and their maxima. */
    std::vector<float> delayGrDbPerBlock, reverbGrDbPerBlock;
    float maxDelayGrDb = 0.0f;
    float maxReverbGrDb = 0.0f;
    /** True if any block keyed from the sidechain bus. */
    bool usedExternalKey = false;
    /** Tail the processor reported when the render started (seconds). */
    double tailSeconds = 0.0;
};

/** Applies one override to an APVTS parameter. Returns an error message, or empty on success. */
std::string applyParameterOverride(juce::AudioProcessorValueTreeState& apvts, const std::string& id,
                                   const std::string& value);

/** Renders `input` (mono or stereo; mono is duplicated) through a fresh processor. */
RenderOutput renderThroughProcessor(const juce::AudioBuffer<float>& input,
                                    const RenderSettings& settings);

} // namespace clearspace::render
