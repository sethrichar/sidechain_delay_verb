#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>

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

constexpr int numOutputChannels = 2;

bool asBool(const std::atomic<float>* p)
{
    return p != nullptr && p->load(std::memory_order_relaxed) >= 0.5f;
}

float asFloat(const std::atomic<float>* p)
{
    return p != nullptr ? p->load(std::memory_order_relaxed) : 0.0f;
}

int asChoice(const std::atomic<float>* p)
{
    return static_cast<int>(std::lround(asFloat(p)));
}
} // namespace

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

    auto get = [this](const char* id) { return apvts.getRawParameterValue(id); };
    raw.inputTrim = get(ParamID::inputTrim);
    raw.outputTrim = get(ParamID::outputTrim);
    raw.mix = get(ParamID::mix);
    raw.routing = get(ParamID::routing);
    raw.duckSource = get(ParamID::duckSource);
    raw.duckLink = get(ParamID::duckLink);
    raw.delayBypass = get(ParamID::delayBypass);
    raw.delayTime = get(ParamID::delayTime);
    raw.delayFeedback = get(ParamID::delayFeedback);
    raw.delayLevel = get(ParamID::delayLevel);
    raw.reverbBypass = get(ParamID::reverbBypass);
    raw.reverbDecay = get(ParamID::reverbDecay);
    raw.reverbLevel = get(ParamID::reverbLevel);
    raw.delayDuck = {get(ParamID::delayDuckEnable),    get(ParamID::delayDuckDepth),
                     get(ParamID::delayDuckThreshold), get(ParamID::delayDuckAttack),
                     get(ParamID::delayDuckHold),      get(ParamID::delayDuckRelease),
                     get(ParamID::delayDuckKeyHPF)};
    raw.reverbDuck = {get(ParamID::reverbDuckEnable),    get(ParamID::reverbDuckDepth),
                      get(ParamID::reverbDuckThreshold), get(ParamID::reverbDuckAttack),
                      get(ParamID::reverbDuckHold),      get(ParamID::reverbDuckRelease),
                      get(ParamID::reverbDuckKeyHPF)};
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

    // Sidechain: off, mono, or stereo (summed to mono as the key).
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

// ---- parameters -------------------------------------------------------------------------

dsp::DuckerParams ClearSpaceProcessor::readDuck(const Raw::Duck& d)
{
    dsp::DuckerParams p;
    p.enabled = asBool(d.enable);
    p.depthDb = asFloat(d.depth);
    p.thresholdDb = asFloat(d.threshold);
    p.attackMs = asFloat(d.attack);
    p.holdMs = asFloat(d.hold);
    p.releaseMs = asFloat(d.release);
    p.keyHpfHz = asFloat(d.keyHPF);
    return p;
}

dsp::RoutingParams ClearSpaceProcessor::readRouting() const
{
    dsp::RoutingParams p;
    p.inputTrimDb = asFloat(raw.inputTrim);
    p.outputTrimDb = asFloat(raw.outputTrim);
    p.mixPercent = asFloat(raw.mix);
    p.serial = asChoice(raw.routing) == 0; // routingChoices(): Serial, Parallel
    p.delayBypass = asBool(raw.delayBypass);
    p.reverbBypass = asBool(raw.reverbBypass);
    p.delayLevelDb = asFloat(raw.delayLevel);
    p.reverbLevelDb = asFloat(raw.reverbLevel);
    return p;
}

void ClearSpaceProcessor::updateParameters()
{
    routing.setParams(readRouting());
    delay.setParams(asFloat(raw.delayTime), asFloat(raw.delayFeedback));
    reverb.setDecay(asFloat(raw.reverbDecay));

    const auto delayDuckParams = readDuck(raw.delayDuck);
    delayDucker.setParams(delayDuckParams);
    // duckLink: the reverb ducker mirrors every delay-ducker value (SPEC §3).
    reverbDucker.setParams(asBool(raw.duckLink) ? delayDuckParams : readDuck(raw.reverbDuck));
}

double ClearSpaceProcessor::getTailLengthSeconds() const
{
    return dsp::Routing::tailSeconds(asFloat(raw.delayTime), asFloat(raw.delayFeedback),
                                     asBool(raw.delayBypass), asFloat(raw.reverbDecay),
                                     asBool(raw.reverbBypass));
}

// ---- lifecycle --------------------------------------------------------------------------

void ClearSpaceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    maxBlockSize = std::max(1, samplesPerBlock);

    dryBuf.setSize(numOutputChannels, maxBlockSize);
    keyBuf.setSize(1, maxBlockSize);
    delayWetBuf.setSize(numOutputChannels, maxBlockSize);
    reverbInBuf.setSize(numOutputChannels, maxBlockSize);
    reverbWetBuf.setSize(numOutputChannels, maxBlockSize);
    for (auto* b : {&dryBuf, &keyBuf, &delayWetBuf, &reverbInBuf, &reverbWetBuf})
        b->clear();

    routing.prepare(sampleRate);
    delayDucker.prepare(sampleRate, maxBlockSize);
    reverbDucker.prepare(sampleRate, maxBlockSize);
    delay.prepare(sampleRate, maxBlockSize);
    reverb.prepare(sampleRate, maxBlockSize);

    updateParameters();
    routing.reset(); // snap every smoother so the first block has no fade-in
    delayDucker.reset();
    reverbDucker.reset();
    delay.reset();
    reverb.reset();

    delayGrDb.store(0.0f, std::memory_order_relaxed);
    reverbGrDb.store(0.0f, std::memory_order_relaxed);
    setLatencySamples(0); // zero added latency by design (CLAUDE.md §3)
}

void ClearSpaceProcessor::releaseResources() {}

// ---- audio ------------------------------------------------------------------------------

void ClearSpaceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (maxBlockSize <= 0) // processBlock before prepareToPlay: pass through
        return;

    auto mainIn = getBusBuffer(buffer, true, mainInputBus);
    auto mainOut = getBusBuffer(buffer, false, mainOutputBus);
    const auto numSamples = buffer.getNumSamples();
    const auto numInCh = mainIn.getNumChannels();
    const auto numOutCh = mainOut.getNumChannels();
    if (numInCh < 1 || numOutCh < 1)
        return;

    // Key source: sidechain bus when selected and connected, else the (trimmed) dry input.
    const bool wantExternal =
        asChoice(raw.duckSource) == 1; // duckSourceChoices(): Internal, External
    const float* keyPtrs[2] = {nullptr, nullptr};
    int numKeyCh = 0;
    if (wantExternal && isSidechainConnected() && getBusCount(true) > sidechainBus)
    {
        auto sc = getBusBuffer(buffer, true, sidechainBus);
        numKeyCh = std::min(2, sc.getNumChannels());
        for (int ch = 0; ch < numKeyCh; ++ch)
            keyPtrs[ch] = sc.getReadPointer(ch);
    }
    usingExternalKey.store(numKeyCh > 0, std::memory_order_relaxed);

    updateParameters();

    // Hosts must not exceed the prepared block size, but be safe: process in sub-blocks so the
    // work buffers never overflow. Inputs are copied out before any output is written because
    // in the mono-in layout output channel 1 aliases the first sidechain channel.
    const float* inPtrs[2] = {mainIn.getReadPointer(0),
                              mainIn.getReadPointer(std::min(1, numInCh - 1))};
    float* outPtrs[2] = {mainOut.getWritePointer(0),
                         mainOut.getWritePointer(std::min(1, numOutCh - 1))};

    for (int pos = 0; pos < numSamples; pos += maxBlockSize)
    {
        const int n = std::min(maxBlockSize, numSamples - pos);
        const float* in[2] = {inPtrs[0] + pos, inPtrs[1] + pos};
        const float* key[2] = {keyPtrs[0] != nullptr ? keyPtrs[0] + pos : nullptr,
                               keyPtrs[1] != nullptr ? keyPtrs[1] + pos : nullptr};
        float* out[2] = {outPtrs[0] + pos, outPtrs[1] + pos};
        processSubBlock(in, numInCh, key, numKeyCh, out, n);
    }

    // Any output channels beyond stereo must not carry garbage.
    for (int ch = numOutputChannels; ch < numOutCh; ++ch)
        mainOut.clear(ch, 0, numSamples);

    delayGrDb.store(delayDucker.gainReductionDb(), std::memory_order_relaxed);
    reverbGrDb.store(reverbDucker.gainReductionDb(), std::memory_order_relaxed);
}

void ClearSpaceProcessor::processSubBlock(const float* const* in, int numInputChannels,
                                          const float* const* key, int numKeyChannels,
                                          float* const* out, int n)
{
    float* dry[2] = {dryBuf.getWritePointer(0), dryBuf.getWritePointer(1)};
    float* keyMono = keyBuf.getWritePointer(0);
    float* delayWet[2] = {delayWetBuf.getWritePointer(0), delayWetBuf.getWritePointer(1)};
    float* reverbIn[2] = {reverbInBuf.getWritePointer(0), reverbInBuf.getWritePointer(1)};
    float* reverbWet[2] = {reverbWetBuf.getWritePointer(0), reverbWetBuf.getWritePointer(1)};

    // 1. Dry copy (mono input duplicated) and input trim.
    for (int ch = 0; ch < numOutputChannels; ++ch)
        std::copy_n(in[std::min(ch, numInputChannels - 1)], n, dry[ch]);
    routing.applyInputTrim(dry, numOutputChannels, n);

    // 2. Key: mono sum of the external sidechain, or of the trimmed dry input.
    if (numKeyChannels >= 2)
        for (int i = 0; i < n; ++i)
            keyMono[i] = 0.5f * (key[0][i] + key[1][i]);
    else if (numKeyChannels == 1)
        std::copy_n(key[0], n, keyMono);
    else if (numInputChannels >= 2)
        for (int i = 0; i < n; ++i)
            keyMono[i] = 0.5f * (dry[0][i] + dry[1][i]);
    else
        std::copy_n(dry[0], n, keyMono);

    // 3. Delay → level × bypass → duck.
    delay.process(dry, delayWet, n);
    routing.applyDelayReturn(delayWet, numOutputChannels, n);
    delayDucker.process(keyMono, delayWet, numOutputChannels, n);

    // 4. Reverb input: dry (+ the ducked delay return when Serial) → reverb → level × bypass →
    // duck.
    routing.buildReverbInput(dry, delayWet, reverbIn, numOutputChannels, n);
    reverb.process(reverbIn, reverbWet, n);
    routing.applyReverbReturn(reverbWet, numOutputChannels, n);
    reverbDucker.process(keyMono, reverbWet, numOutputChannels, n);

    // 5. Equal-power mix and output trim, written to the host buffer last.
    routing.mixOutput(dry, delayWet, reverbWet, out, numOutputChannels, n);
}

// ---- editor / state ---------------------------------------------------------------------

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
