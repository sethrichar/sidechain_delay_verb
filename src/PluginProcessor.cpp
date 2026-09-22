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

constexpr int routingSerialIndex = 0;      // params::routingChoices(): Serial, Parallel
constexpr int duckSourceExternalIndex = 1; // params::duckSourceChoices(): Internal, External
constexpr int stereoModePingPongIndex = 1; // params::delayStereoModeChoices(): Stereo, PingPong

bool asBool(const std::atomic<float>* p) noexcept
{
    return p->load(std::memory_order_relaxed) >= 0.5f;
}

float asFloat(const std::atomic<float>* p) noexcept
{
    return p->load(std::memory_order_relaxed);
}

int asIndex(const std::atomic<float>* p) noexcept
{
    return static_cast<int>(p->load(std::memory_order_relaxed) + 0.5f);
}
} // namespace

dsp::Ducker::Params ClearSpaceProcessor::RawParams::Duck::read() const noexcept
{
    dsp::Ducker::Params p;
    p.enabled = asBool(enable);
    p.depthDb = asFloat(depth);
    p.thresholdDb = asFloat(threshold);
    p.attackMs = asFloat(attack);
    p.holdMs = asFloat(hold);
    p.releaseMs = asFloat(release);
    p.keyHpfHz = asFloat(keyHPF);
    return p;
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
    cacheParameterPointers();
    effectiveDelayMs.store(resolveDelayTimeMs(), std::memory_order_relaxed);
}

void ClearSpaceProcessor::cacheParameterPointers()
{
    auto get = [this](const char* id)
    {
        auto* p = apvts.getRawParameterValue(id);
        jassert(p != nullptr);
        return p;
    };

    raw.inputTrim = get(ParamID::inputTrim);
    raw.outputTrim = get(ParamID::outputTrim);
    raw.mix = get(ParamID::mix);
    raw.routing = get(ParamID::routing);
    raw.duckSource = get(ParamID::duckSource);
    raw.duckLink = get(ParamID::duckLink);
    raw.delayBypass = get(ParamID::delayBypass);
    raw.delayMode = get(ParamID::delayMode);
    raw.delayTime = get(ParamID::delayTime);
    raw.delaySync = get(ParamID::delaySync);
    raw.delayNote = get(ParamID::delayNote);
    raw.delayNoteMod = get(ParamID::delayNoteMod);
    raw.delayFeedback = get(ParamID::delayFeedback);
    raw.delayLowCut = get(ParamID::delayLowCut);
    raw.delayHighCut = get(ParamID::delayHighCut);
    raw.delayMod = get(ParamID::delayMod);
    raw.delayModRate = get(ParamID::delayModRate);
    raw.delayStereoMode = get(ParamID::delayStereoMode);
    raw.delayLevel = get(ParamID::delayLevel);
    raw.reverbBypass = get(ParamID::reverbBypass);
    raw.reverbMode = get(ParamID::reverbMode);
    raw.reverbPreDelay = get(ParamID::reverbPreDelay);
    raw.reverbDecay = get(ParamID::reverbDecay);
    raw.reverbSize = get(ParamID::reverbSize);
    raw.reverbDamping = get(ParamID::reverbDamping);
    raw.reverbLowCut = get(ParamID::reverbLowCut);
    raw.reverbHighCut = get(ParamID::reverbHighCut);
    raw.reverbDiffusion = get(ParamID::reverbDiffusion);
    raw.reverbModRate = get(ParamID::reverbModRate);
    raw.reverbModDepth = get(ParamID::reverbModDepth);
    raw.reverbWidth = get(ParamID::reverbWidth);
    raw.reverbLevel = get(ParamID::reverbLevel);

    raw.delayDuck.enable = get(ParamID::delayDuckEnable);
    raw.delayDuck.depth = get(ParamID::delayDuckDepth);
    raw.delayDuck.threshold = get(ParamID::delayDuckThreshold);
    raw.delayDuck.attack = get(ParamID::delayDuckAttack);
    raw.delayDuck.hold = get(ParamID::delayDuckHold);
    raw.delayDuck.release = get(ParamID::delayDuckRelease);
    raw.delayDuck.keyHPF = get(ParamID::delayDuckKeyHPF);

    raw.reverbDuck.enable = get(ParamID::reverbDuckEnable);
    raw.reverbDuck.depth = get(ParamID::reverbDuckDepth);
    raw.reverbDuck.threshold = get(ParamID::reverbDuckThreshold);
    raw.reverbDuck.attack = get(ParamID::reverbDuckAttack);
    raw.reverbDuck.hold = get(ParamID::reverbDuckHold);
    raw.reverbDuck.release = get(ParamID::reverbDuckRelease);
    raw.reverbDuck.keyHPF = get(ParamID::reverbDuckKeyHPF);
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

float ClearSpaceProcessor::resolveDelayTimeMs() const noexcept
{
    const auto mode = static_cast<dsp::DelayEngine::Mode>(asIndex(raw.delayMode));
    float ms = asFloat(raw.delayTime);
    if (asBool(raw.delaySync))
        ms = static_cast<float>(1000.0 * dsp::DelayEngine::noteLengthSeconds(
                                             hostBpm.load(std::memory_order_relaxed),
                                             asIndex(raw.delayNote), asIndex(raw.delayNoteMod)));
    return dsp::DelayEngine::clampTimeMs(mode, ms);
}

void ClearSpaceProcessor::readHostTempo()
{
    // Only the playhead's BPM is used; a stopped transport still reports its tempo so sync
    // works while auditioning. No playhead (Standalone) → fallback.
    double bpm = dsp::DelayEngine::fallbackBpm;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto hostTempo = position->getBpm())
                if (*hostTempo > 0.0)
                    bpm = *hostTempo;
    hostBpm.store(bpm, std::memory_order_relaxed);
}

double ClearSpaceProcessor::getTailLengthSeconds() const
{
    // Read straight from the parameter atomics so this is correct from any thread, even
    // before the first processBlock. Mirrors Routing::getTailSeconds.
    const bool serial = asIndex(raw.routing) == routingSerialIndex;
    const double delayTail = asBool(raw.delayBypass)
                                 ? 0.0
                                 : dsp::DelayEngine::tailSecondsFor(
                                       static_cast<dsp::DelayEngine::Mode>(asIndex(raw.delayMode)),
                                       effectiveDelayMs.load(std::memory_order_relaxed),
                                       asFloat(raw.delayFeedback) / 100.0f);
    const double reverbTail =
        asBool(raw.reverbBypass)
            ? 0.0
            : dsp::ReverbEngine::tailSecondsFor(
                  static_cast<dsp::ReverbEngine::Mode>(asIndex(raw.reverbMode)),
                  asFloat(raw.reverbDecay), asFloat(raw.reverbPreDelay),
                  asFloat(raw.reverbSize) / 100.0f);
    return serial ? delayTail + reverbTail : std::max(delayTail, reverbTail);
}

void ClearSpaceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max(1, samplesPerBlock);

    inBuf.setSize(2, maxBlock);
    keyBuf.setSize(1, maxBlock);
    gainBuf.setSize(2, maxBlock);
    inBuf.clear();
    keyBuf.clear();
    gainBuf.clear();

    inputGain.reset(sampleRate, inputTrimSmoothingMs * 1.0e-3);
    delayDucker.prepare(sampleRate, maxBlock);
    reverbDucker.prepare(sampleRate, maxBlock);
    delayEffect.prepare(sampleRate, maxBlock);
    reverbEffect.prepare(sampleRate, maxBlock);
    routing.prepare(sampleRate, maxBlock);

    // Load the current parameter values, then snap every smoother to them so playback
    // starts from the saved state rather than ramping out of the defaults.
    updateFromParameters();
    inputGain.setCurrentAndTargetValue(inputGain.getTargetValue());
    delayEffect.reset();
    reverbEffect.reset();
    routing.reset();
    delayGrDb.store(0.0f, std::memory_order_relaxed);
    reverbGrDb.store(0.0f, std::memory_order_relaxed);
}

void ClearSpaceProcessor::releaseResources() {}

void ClearSpaceProcessor::updateFromParameters()
{
    inputGain.setTargetValue(juce::Decibels::decibelsToGain(asFloat(raw.inputTrim)));

    const auto delayDuck = raw.delayDuck.read();
    delayDucker.setParams(delayDuck);
    reverbDucker.setParams(asBool(raw.duckLink) ? delayDuck : raw.reverbDuck.read());

    dsp::DelayEngine::Params d;
    d.mode = static_cast<dsp::DelayEngine::Mode>(asIndex(raw.delayMode));
    d.timeMs = resolveDelayTimeMs();
    d.feedback = asFloat(raw.delayFeedback) / 100.0f;
    d.lowCutHz = asFloat(raw.delayLowCut);
    d.highCutHz = asFloat(raw.delayHighCut);
    d.modDepth = asFloat(raw.delayMod) / 100.0f;
    d.modRateHz = asFloat(raw.delayModRate);
    d.pingPong = asIndex(raw.delayStereoMode) == stereoModePingPongIndex;
    delayEffect.setParams(d);
    effectiveDelayMs.store(d.timeMs, std::memory_order_relaxed);

    dsp::ReverbEngine::Params r;
    r.mode = static_cast<dsp::ReverbEngine::Mode>(asIndex(raw.reverbMode));
    r.preDelayMs = asFloat(raw.reverbPreDelay);
    r.decaySeconds = asFloat(raw.reverbDecay);
    r.size = asFloat(raw.reverbSize) / 100.0f;
    r.dampingHz = asFloat(raw.reverbDamping);
    r.lowCutHz = asFloat(raw.reverbLowCut);
    r.highCutHz = asFloat(raw.reverbHighCut);
    r.diffusion = asFloat(raw.reverbDiffusion) / 100.0f;
    r.modRateHz = asFloat(raw.reverbModRate);
    r.modDepth = asFloat(raw.reverbModDepth) / 100.0f;
    r.width = asFloat(raw.reverbWidth) / 100.0f;
    reverbEffect.setParams(r);

    dsp::Routing::Params p;
    p.serial = asIndex(raw.routing) == routingSerialIndex;
    p.mixPercent = asFloat(raw.mix);
    p.outputTrimDb = asFloat(raw.outputTrim);
    p.delayBypass = asBool(raw.delayBypass);
    p.delayLevelDb = asFloat(raw.delayLevel);
    p.reverbBypass = asBool(raw.reverbBypass);
    p.reverbLevelDb = asFloat(raw.reverbLevel);
    routing.setParams(p);
}

void ClearSpaceProcessor::processChunk(const float* inL, const float* inR, const float* keyL,
                                       const float* keyR, float* outL, float* outR, int numSamples)
{
    auto* tL = inBuf.getWritePointer(0);
    auto* tR = inBuf.getWritePointer(1);
    auto* key = keyBuf.getWritePointer(0);
    auto* delayGain = gainBuf.getWritePointer(0);
    auto* reverbGain = gainBuf.getWritePointer(1);

    // Input trim → "in".
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inputGain.getNextValue();
        tL[i] = inL[i] * g;
        tR[i] = inR[i] * g;
    }

    // Key: mono sum of the chosen source. External = the sidechain bus (not trimmed);
    // internal = the post-trim input.
    if (keyL != nullptr)
    {
        if (keyR != nullptr)
            for (int i = 0; i < numSamples; ++i)
                key[i] = 0.5f * (keyL[i] + keyR[i]);
        else
            juce::FloatVectorOperations::copy(key, keyL, numSamples);
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
            key[i] = 0.5f * (tL[i] + tR[i]);
    }

    delayDucker.process(key, delayGain, numSamples);
    reverbDucker.process(key, reverbGain, numSamples);

    routing.process(tL, tR, delayGain, reverbGain, delayEffect, reverbEffect, outL, outR,
                    numSamples);
}

void ClearSpaceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;
    if (maxBlock <= 0)
    {
        buffer.clear();
        return; // processBlock before prepareToPlay: nothing is sized yet
    }

    readHostTempo();
    updateFromParameters();

    auto mainIn = getBusBuffer(buffer, true, mainInputBus);
    auto mainOut = getBusBuffer(buffer, false, mainOutputBus);
    const auto numIn = mainIn.getNumChannels();
    const auto numOut = mainOut.getNumChannels();
    if (numIn < 1 || numOut < 2)
    {
        buffer.clear();
        return;
    }

    // External key from the sidechain bus, if selected and actually connected.
    const float* keyL = nullptr;
    const float* keyR = nullptr;
    bool external = false;
    if (asIndex(raw.duckSource) == duckSourceExternalIndex && getBusCount(true) > sidechainBus)
    {
        auto sc = getBusBuffer(buffer, true, sidechainBus);
        if (sc.getNumChannels() >= 1)
        {
            keyL = sc.getReadPointer(0);
            keyR = sc.getNumChannels() >= 2 ? sc.getReadPointer(1) : nullptr;
            external = true;
        }
    }
    usingExternalKey.store(external, std::memory_order_relaxed);

    // Input channel pointers. With a mono main input, output channel 1 may alias the first
    // sidechain channel, so nothing is written to the output until a chunk is fully computed
    // (processChunk copies its inputs before writing).
    const float* inL = mainIn.getReadPointer(0);
    const float* inR = numIn >= 2 ? mainIn.getReadPointer(1) : inL;
    float* outL = mainOut.getWritePointer(0);
    float* outR = mainOut.getWritePointer(1);

    float delayPeak = 0.0f, reverbPeak = 0.0f;

    // Hosts occasionally exceed the prepared block size; chunk rather than overrun.
    for (int pos = 0; pos < numSamples; pos += maxBlock)
    {
        const int n = std::min(maxBlock, numSamples - pos);
        processChunk(inL + pos, inR + pos, keyL != nullptr ? keyL + pos : nullptr,
                     keyR != nullptr ? keyR + pos : nullptr, outL + pos, outR + pos, n);
        delayPeak = std::max(delayPeak, delayDucker.getBlockPeakGainReductionDb());
        reverbPeak = std::max(reverbPeak, reverbDucker.getBlockPeakGainReductionDb());
    }

    delayGrDb.store(delayPeak, std::memory_order_relaxed);
    reverbGrDb.store(reverbPeak, std::memory_order_relaxed);

    // Any output channels beyond stereo must not carry garbage.
    for (int ch = 2; ch < numOut; ++ch)
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
