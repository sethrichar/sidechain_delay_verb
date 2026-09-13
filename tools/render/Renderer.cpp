#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace clearspace::render
{

namespace
{

bool parseBool(const juce::String& text, bool& out)
{
    const auto lower = text.trim().toLowerCase();
    if (lower == "1" || lower == "on" || lower == "true" || lower == "yes")
    {
        out = true;
        return true;
    }
    if (lower == "0" || lower == "off" || lower == "false" || lower == "no")
    {
        out = false;
        return true;
    }
    return false;
}

bool isNumeric(const juce::String& text)
{
    const auto t = text.trim();
    return t.isNotEmpty() && t.containsOnly("0123456789.-+eE") && t.containsAnyOf("0123456789");
}

} // namespace

std::string applyParameterOverride(juce::AudioProcessorValueTreeState& apvts, const std::string& id,
                                   const std::string& valueText)
{
    auto* param = apvts.getParameter(juce::String(id));
    if (param == nullptr)
        return "unknown parameter '" + id + "'";

    const juce::String text(valueText);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param))
    {
        int index = -1;
        if (isNumeric(text))
            index = text.getIntValue();
        else
            index = choice->choices.indexOf(text, /*ignoreCase*/ true);

        if (index < 0 || index >= choice->choices.size())
            return "parameter '" + id + "': '" + valueText + "' is not one of " +
                   choice->choices.joinIntoString(", ").toStdString();

        choice->setValueNotifyingHost(choice->convertTo0to1(static_cast<float>(index)));
        return {};
    }

    if (auto* boolean = dynamic_cast<juce::AudioParameterBool*>(param))
    {
        bool value = false;
        if (!parseBool(text, value))
            return "parameter '" + id + "': expected on/off, got '" + valueText + "'";
        boolean->setValueNotifyingHost(value ? 1.0f : 0.0f);
        return {};
    }

    if (!isNumeric(text))
        return "parameter '" + id + "': expected a number, got '" + valueText + "'";

    const auto value = text.getFloatValue();
    const auto& range = param->getNormalisableRange();
    if (value < range.start || value > range.end)
        return "parameter '" + id + "': " + valueText + " is outside " +
               juce::String(range.start).toStdString() + ".." +
               juce::String(range.end).toStdString();

    param->setValueNotifyingHost(param->convertTo0to1(value));
    return {};
}

RenderOutput renderThroughProcessor(const juce::AudioBuffer<float>& input,
                                    const RenderSettings& settings)
{
    RenderOutput result;

    if (settings.blockSize < 1)
    {
        result.errors.push_back({"block size must be >= 1"});
        return result;
    }
    if (settings.sampleRate <= 0.0)
    {
        result.errors.push_back({"sample rate must be > 0"});
        return result;
    }

    ClearSpaceProcessor processor;

    // Main in/out stereo, sidechain disabled (Phase 1 adds --sidechain).
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set(ClearSpaceProcessor::mainInputBus, juce::AudioChannelSet::stereo());
    layout.outputBuses.set(ClearSpaceProcessor::mainOutputBus, juce::AudioChannelSet::stereo());
    if (layout.inputBuses.size() > ClearSpaceProcessor::sidechainBus)
        layout.inputBuses.set(ClearSpaceProcessor::sidechainBus, juce::AudioChannelSet::disabled());
    if (!processor.setBusesLayout(layout))
    {
        result.errors.push_back({"processor rejected stereo in/out bus layout"});
        return result;
    }

    for (const auto& [id, value] : settings.parameterOverrides)
    {
        auto error = applyParameterOverride(processor.getAPVTS(), id, value);
        if (!error.empty())
            result.errors.push_back({error});
    }
    if (!result.ok())
        return result;

    processor.setRateAndBufferSizeDetails(settings.sampleRate, settings.blockSize);
    processor.prepareToPlay(settings.sampleRate, settings.blockSize);

    const auto inputSamples = input.getNumSamples();
    const auto tailSamples =
        settings.appendTail
            ? static_cast<int>(std::ceil(processor.getTailLengthSeconds() * settings.sampleRate))
            : 0;
    const auto totalSamples = inputSamples + tailSamples;

    const auto numChannels =
        std::max(processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
    juce::AudioBuffer<float> block(std::max(2, numChannels), settings.blockSize);
    juce::MidiBuffer midi;

    result.buffer.setSize(2, std::max(0, totalSamples));
    result.buffer.clear();

    for (int pos = 0; pos < totalSamples; pos += settings.blockSize)
    {
        const auto n = std::min(settings.blockSize, totalSamples - pos);
        block.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto srcCh = std::min(ch, input.getNumChannels() - 1);
            const auto available = std::max(0, std::min(n, inputSamples - pos));
            if (srcCh >= 0 && available > 0)
                block.copyFrom(ch, 0, input, srcCh, pos, available);
        }

        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), block.getNumChannels(), n);
        midi.clear();
        processor.processBlock(view, midi);

        for (int ch = 0; ch < 2; ++ch)
            result.buffer.copyFrom(ch, pos, view, ch, 0, n);
    }

    processor.releaseResources();
    return result;
}

} // namespace clearspace::render
