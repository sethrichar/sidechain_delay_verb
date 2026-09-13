#include "PluginProcessor.h"

#include "PluginEditor.h"

#ifndef JucePlugin_Name
// Console-app consumers (render tool, tests) compile this file without the plugin-client
// defines; the plugin targets get the real value from juce_add_plugin.
#define JucePlugin_Name "Clear Space"
#endif

namespace clearspace
{

namespace
{
const juce::Identifier stateVersionProperty("stateVersion");
}

ClearSpaceProcessor::BusesProperties ClearSpaceProcessor::makeBuses()
{
    return BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false);
}

ClearSpaceProcessor::ClearSpaceProcessor()
    : AudioProcessor(makeBuses())
    , apvts(*this, nullptr, "ClearSpace", params::createLayout())
{
    apvts.state.setProperty(stateVersionProperty, params::stateVersion, nullptr);
}

const juce::String ClearSpaceProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ClearSpaceProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto mono = juce::AudioChannelSet::mono();

    // Output is always stereo; input may be mono (mono vocal insert) or stereo.
    if (mainOut != stereo)
        return false;
    if (mainIn != stereo && mainIn != mono)
        return false;

    // Sidechain: off, mono, or stereo (summed to mono as the key in Phase 1).
    if (layouts.inputBuses.size() > sidechainBus)
    {
        const auto sc = layouts.inputBuses.getReference(sidechainBus);
        if (!sc.isDisabled() && sc != mono && sc != stereo)
            return false;
    }

    return true;
}

bool ClearSpaceProcessor::isSidechainConnected() const
{
    if (getBusCount(true) <= sidechainBus)
        return false;
    const auto* bus = getBus(true, sidechainBus);
    return bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
}

void ClearSpaceProcessor::prepareToPlay(double, int)
{
    // Phase 0: nothing to allocate. DSP buffers are sized here from Phase 1 on.
}

void ClearSpaceProcessor::releaseResources() {}

void ClearSpaceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainIn = getBusBuffer(buffer, true, mainInputBus);
    auto mainOut = getBusBuffer(buffer, false, mainOutputBus);
    const auto numSamples = buffer.getNumSamples();

    // Main input and output share buffer channels 0..N-1, so a stereo→stereo pass-through
    // is already done. Mono→stereo: duplicate the single input channel. Do this LAST in any
    // future processing order, because out channel 1 may alias the first sidechain channel.
    if (mainIn.getNumChannels() == 1 && mainOut.getNumChannels() == 2)
        mainOut.copyFrom(1, 0, mainIn, 0, 0, numSamples);

    // Any output channels beyond what we wrote must not carry garbage.
    for (int ch = std::max(mainIn.getNumChannels(), 2); ch < mainOut.getNumChannels(); ++ch)
        mainOut.clear(ch, 0, numSamples);
}

juce::AudioProcessorEditor* ClearSpaceProcessor::createEditor()
{
    return new ClearSpaceEditor(*this);
}

void ClearSpaceProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty(stateVersionProperty, params::stateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void ClearSpaceProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;

    auto incoming = juce::ValueTree::fromXml(*xml);
    // Phase 7 adds the migration hook keyed on stateVersionProperty here.
    apvts.replaceState(incoming);
    apvts.state.setProperty(stateVersionProperty, params::stateVersion, nullptr);
}

} // namespace clearspace

// JUCE's plugin-client entry point (prototype lives in juce_audio_processors).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new clearspace::ClearSpaceProcessor();
}
