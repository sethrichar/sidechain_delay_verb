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

/** A playing transport at a fixed tempo, 4/4, starting at bar 1. */
class FixedTempoPlayHead final : public juce::AudioPlayHead
{
public:
    FixedTempoPlayHead(double bpmToReport, double sampleRate)
        : bpm(bpmToReport)
        , fs(sampleRate)
    {
    }

    void setPositionSamples(juce::int64 samples) noexcept { positionSamples = samples; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        info.setTimeSignature(TimeSignature{4, 4});
        info.setIsPlaying(true);
        info.setIsRecording(false);
        info.setTimeInSamples(positionSamples);
        const double seconds = static_cast<double>(positionSamples) / fs;
        info.setTimeInSeconds(seconds);
        info.setPpqPosition(seconds * bpm / 60.0);
        info.setPpqPositionOfLastBarStart(0.0);
        return info;
    }

    bool canControlTransport() override { return false; }

private:
    double bpm;
    double fs;
    juce::int64 positionSamples = 0;
};

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

    // Declared before the processor so it outlives it.
    FixedTempoPlayHead playHead(settings.bpm.value_or(0.0), settings.sampleRate);
    ClearSpaceProcessor processor;
    if (settings.bpm.has_value())
        processor.setPlayHead(&playHead);

    // Main in/out stereo; sidechain disabled unless a key buffer was supplied.
    const int sidechainChannels = std::min(2, settings.sidechain.getNumChannels());
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set(ClearSpaceProcessor::mainInputBus, juce::AudioChannelSet::stereo());
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
    if (sidechainChannels > 0 && !processor.isSidechainConnected())
    {
        result.errors.push_back({"sidechain bus did not enable"});
        return result;
    }

    for (const auto& [id, value] : settings.parameterOverrides)
    {
        auto error = applyParameterOverride(processor.getAPVTS(), id, value);
        if (!error.empty())
            result.errors.push_back({error});
    }
    for (const auto& point : settings.automation)
    {
        // Validate up front so a typo fails the render instead of silently doing nothing.
        if (processor.getAPVTS().getParameter(juce::String(point.id)) == nullptr)
            result.errors.push_back({"automation: unknown parameter '" + point.id + "'"});
        if (point.timeSeconds < 0.0)
            result.errors.push_back({"automation: negative time for '" + point.id + "'"});
    }
    if (!result.ok())
        return result;

    auto automation = settings.automation;
    std::stable_sort(automation.begin(), automation.end(),
                     [](const AutomationPoint& a, const AutomationPoint& b)
                     { return a.timeSeconds < b.timeSeconds; });
    size_t nextAutomation = 0;

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

    const int sidechainOffset = sidechainChannels > 0
                                    ? processor.getChannelIndexInProcessBlockBuffer(
                                          true, ClearSpaceProcessor::sidechainBus, 0)
                                    : -1;

    for (int pos = 0; pos < totalSamples; pos += settings.blockSize)
    {
        const auto n = std::min(settings.blockSize, totalSamples - pos);
        const double blockStart = static_cast<double>(pos) / settings.sampleRate;

        while (nextAutomation < automation.size() &&
               automation[nextAutomation].timeSeconds <= blockStart + 1.0e-9)
        {
            const auto& point = automation[nextAutomation++];
            auto error = applyParameterOverride(processor.getAPVTS(), point.id, point.value);
            if (!error.empty())
            {
                result.errors.push_back({"automation: " + error});
                return result;
            }
        }

        block.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto srcCh = std::min(ch, input.getNumChannels() - 1);
            const auto available = std::max(0, std::min(n, inputSamples - pos));
            if (srcCh >= 0 && available > 0)
                block.copyFrom(ch, 0, input, srcCh, pos, available);
        }

        for (int ch = 0; ch < sidechainChannels; ++ch)
        {
            const auto available =
                std::max(0, std::min(n, settings.sidechain.getNumSamples() - pos));
            if (available > 0 && sidechainOffset >= 0 &&
                sidechainOffset + ch < block.getNumChannels())
                block.copyFrom(sidechainOffset + ch, 0, settings.sidechain, ch, pos, available);
        }

        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), block.getNumChannels(), n);
        midi.clear();
        playHead.setPositionSamples(pos);
        processor.processBlock(view, midi);

        for (int ch = 0; ch < 2; ++ch)
            result.buffer.copyFrom(ch, pos, view, ch, 0, n);
    }

    processor.releaseResources();
    processor.setPlayHead(nullptr);
    return result;
}

} // namespace clearspace::render
