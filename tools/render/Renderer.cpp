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

/** Copies `n` samples of `source` channel `srcCh` starting at `pos` into `dest` channel
    `destCh`, zero-filling whatever lies past the end of `source`. */
void copyWindow(juce::AudioBuffer<float>& dest, int destCh, const juce::AudioBuffer<float>& source,
                int srcCh, int pos, int n)
{
    dest.clear(destCh, 0, n);
    if (srcCh < 0 || srcCh >= source.getNumChannels())
        return;
    const auto available = std::max(0, std::min(n, source.getNumSamples() - pos));
    if (available > 0)
        dest.copyFrom(destCh, 0, source, srcCh, pos, available);
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

    // Bus layout: main in mono or stereo, main out stereo, sidechain off / mono / stereo.
    const int sidechainChannels =
        settings.hasSidechain() ? std::min(2, settings.sidechain.getNumChannels()) : 0;
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set(ClearSpaceProcessor::mainInputBus, settings.monoInput
                                                                 ? juce::AudioChannelSet::mono()
                                                                 : juce::AudioChannelSet::stereo());
    layout.outputBuses.set(ClearSpaceProcessor::mainOutputBus, juce::AudioChannelSet::stereo());
    if (layout.inputBuses.size() > ClearSpaceProcessor::sidechainBus)
        layout.inputBuses.set(ClearSpaceProcessor::sidechainBus,
                              sidechainChannels == 0   ? juce::AudioChannelSet::disabled()
                              : sidechainChannels == 1 ? juce::AudioChannelSet::mono()
                                                       : juce::AudioChannelSet::stereo());
    if (!processor.setBusesLayout(layout))
    {
        result.errors.push_back({"processor rejected the requested bus layout"});
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
    result.tailSeconds = processor.getTailLengthSeconds();

    const auto inputSamples = input.getNumSamples();
    const auto tailSamples =
        settings.appendTail ? static_cast<int>(std::ceil(result.tailSeconds * settings.sampleRate))
                            : 0;
    const auto totalSamples = inputSamples + tailSamples;

    // The host-style block buffer holds every input and output channel; JUCE tells us where
    // each bus's channels live in it (they overlap between inputs and outputs).
    const auto numChannels =
        std::max(processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
    juce::AudioBuffer<float> block(std::max(2, numChannels), settings.blockSize);
    juce::MidiBuffer midi;

    const int mainInChannels = settings.monoInput ? 1 : 2;
    int mainInIndex[2] = {0, 0};
    for (int ch = 0; ch < mainInChannels; ++ch)
        mainInIndex[ch] = processor.getChannelIndexInProcessBlockBuffer(
            true, ClearSpaceProcessor::mainInputBus, ch);
    int sidechainIndex[2] = {-1, -1};
    for (int ch = 0; ch < sidechainChannels; ++ch)
        sidechainIndex[ch] = processor.getChannelIndexInProcessBlockBuffer(
            true, ClearSpaceProcessor::sidechainBus, ch);
    int mainOutIndex[2] = {0, 1};
    for (int ch = 0; ch < 2; ++ch)
        mainOutIndex[ch] = processor.getChannelIndexInProcessBlockBuffer(
            false, ClearSpaceProcessor::mainOutputBus, ch);

    result.buffer.setSize(2, std::max(0, totalSamples));
    result.buffer.clear();
    const auto numBlocks = (totalSamples + settings.blockSize - 1) / settings.blockSize;
    result.delayGrDbPerBlock.reserve(static_cast<size_t>(std::max(0, numBlocks)));
    result.reverbGrDbPerBlock.reserve(static_cast<size_t>(std::max(0, numBlocks)));

    int blockIndex = 0;
    for (int pos = 0; pos < totalSamples; pos += settings.blockSize, ++blockIndex)
    {
        const auto n = std::min(settings.blockSize, totalSamples - pos);
        block.clear();

        for (int ch = 0; ch < mainInChannels; ++ch)
            copyWindow(block, mainInIndex[ch], input, std::min(ch, input.getNumChannels() - 1), pos,
                       n);
        for (int ch = 0; ch < sidechainChannels; ++ch)
            copyWindow(block, sidechainIndex[ch], settings.sidechain, ch, pos, n);

        if (settings.perBlockHook)
            settings.perBlockHook(processor, blockIndex, pos);

        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), block.getNumChannels(), n);
        midi.clear();
        processor.processBlock(view, midi);

        for (int ch = 0; ch < 2; ++ch)
            result.buffer.copyFrom(ch, pos, view, mainOutIndex[ch], 0, n);

        const auto delayGr = processor.getDelayGainReductionDb();
        const auto reverbGr = processor.getReverbGainReductionDb();
        result.delayGrDbPerBlock.push_back(delayGr);
        result.reverbGrDbPerBlock.push_back(reverbGr);
        result.maxDelayGrDb = std::max(result.maxDelayGrDb, delayGr);
        result.maxReverbGrDb = std::max(result.maxReverbGrDb, reverbGr);
        result.usedExternalKey = result.usedExternalKey || processor.isUsingExternalKey();
    }

    processor.releaseResources();
    return result;
}

} // namespace clearspace::render
