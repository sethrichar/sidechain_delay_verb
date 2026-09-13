#pragma once

// Drives ClearSpaceProcessor offline: prepare → chunked processBlock → output buffer.

#include "PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

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
    /** Append getTailLengthSeconds() of silence after the input (Phase 1+). */
    bool appendTail = false;
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
};

/** Applies one override to an APVTS parameter. Returns an error message, or empty on success. */
std::string applyParameterOverride(juce::AudioProcessorValueTreeState& apvts, const std::string& id,
                                   const std::string& value);

/** Renders `input` (mono or stereo; mono is duplicated) through a fresh processor. */
RenderOutput renderThroughProcessor(const juce::AudioBuffer<float>& input,
                                    const RenderSettings& settings);

} // namespace clearspace::render
