#pragma once

// Drives ClearSpaceProcessor offline: prepare → chunked processBlock → output buffer.

#include "PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <string>
#include <utility>
#include <vector>

namespace clearspace::render
{

/** A parameter change applied at the first block that starts at or after `timeSeconds`. */
struct AutomationPoint
{
    double timeSeconds = 0.0;
    std::string id;
    std::string value; // same syntax as a --set value
};

struct RenderSettings
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    /** "id=value" pairs. Value is a number in the parameter's natural units, or for
        choice/bool parameters a choice name / "on" / "off". */
    std::vector<std::pair<std::string, std::string>> parameterOverrides;
    /** Timed parameter changes, applied in order of time at block boundaries. */
    std::vector<AutomationPoint> automation;
    /** Append getTailLengthSeconds() of silence after the input. */
    bool appendTail = false;
    /** Optional external key. Empty (0 channels) = sidechain bus disabled. Mono or stereo;
        shorter than the input is padded with silence, longer is truncated. */
    juce::AudioBuffer<float> sidechain;
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

/** Renders `input` (mono or stereo; mono is duplicated) through a fresh processor. With
    `settings.sidechain` set, the sidechain bus is enabled and fed from it. */
RenderOutput renderThroughProcessor(const juce::AudioBuffer<float>& input,
                                    const RenderSettings& settings);

} // namespace clearspace::render
