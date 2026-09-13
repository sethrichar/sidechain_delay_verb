#pragma once

#include "Parameters.h"
#include "dsp/Ducker.h"
#include "dsp/Routing.h"
#include "dsp/standin/StandInDelay.h"
#include "dsp/standin/StandInReverb.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace clearspace
{

/** Clear Space — ducking delay + reverb.

    Phase 1: SPEC §2 signal flow (trim, serial/parallel, level, equal-power mix, click-free
    bypass, tail) and the SPEC §3 ducker on each return, keyed from the input or the sidechain
    bus. The delay and reverb are Phase 1 stand-ins (src/dsp/standin) until Phases 2/3.
    Buses: main in (mono or stereo) → main out (stereo); optional "Sidechain" input
    (mono or stereo, off by default) used as the external key.
*/
class ClearSpaceProcessor final : public juce::AudioProcessor
{
public:
    ClearSpaceProcessor();
    ~ClearSpaceProcessor() override = default;

    // -- AudioProcessor -----------------------------------------------------------------
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // -- Clear Space --------------------------------------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const { return apvts; }

    /** True when the host has connected the sidechain bus (bus 1) with ≥1 channel. */
    bool isSidechainConnected() const;

    /** Gain reduction currently applied to each return, in positive dB (0 = unity).
        Written by the audio thread at the end of every block; read by the UI meter. */
    float getDelayGainReductionDb() const { return delayGrDb.load(std::memory_order_relaxed); }
    float getReverbGainReductionDb() const { return reverbGrDb.load(std::memory_order_relaxed); }

    /** True when the last block took its key from the sidechain bus (External + connected). */
    bool isUsingExternalKey() const { return usingExternalKey.load(std::memory_order_relaxed); }

    static constexpr int mainInputBus = 0;
    static constexpr int sidechainBus = 1;
    static constexpr int mainOutputBus = 0;

private:
    static BusesProperties makeBuses();

    /** Raw APVTS atomics, looked up once in the constructor (no string lookups per block). */
    struct Raw
    {
        std::atomic<float>* inputTrim = nullptr;
        std::atomic<float>* outputTrim = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* routing = nullptr;
        std::atomic<float>* duckSource = nullptr;
        std::atomic<float>* duckLink = nullptr;
        std::atomic<float>* delayBypass = nullptr;
        std::atomic<float>* delayTime = nullptr;
        std::atomic<float>* delayFeedback = nullptr;
        std::atomic<float>* delayLevel = nullptr;
        std::atomic<float>* reverbBypass = nullptr;
        std::atomic<float>* reverbDecay = nullptr;
        std::atomic<float>* reverbLevel = nullptr;
        struct Duck
        {
            std::atomic<float>* enable = nullptr;
            std::atomic<float>* depth = nullptr;
            std::atomic<float>* threshold = nullptr;
            std::atomic<float>* attack = nullptr;
            std::atomic<float>* hold = nullptr;
            std::atomic<float>* release = nullptr;
            std::atomic<float>* keyHPF = nullptr;
        } delayDuck, reverbDuck;
    };

    static dsp::DuckerParams readDuck(const Raw::Duck& d);
    dsp::RoutingParams readRouting() const;
    void updateParameters();
    void processSubBlock(const float* const* in, int numInputChannels, const float* const* key,
                         int numKeyChannels, float* const* out, int n);

    juce::AudioProcessorValueTreeState apvts;
    Raw raw;

    dsp::Routing routing;
    dsp::Ducker delayDucker, reverbDucker;
    dsp::standin::StandInDelay delay;
    dsp::standin::StandInReverb reverb;

    // Work buffers, sized in prepareToPlay. All stereo except key (mono).
    juce::AudioBuffer<float> dryBuf, keyBuf, delayWetBuf, reverbInBuf, reverbWetBuf;
    int maxBlockSize = 0;

    std::atomic<float> delayGrDb{0.0f};
    std::atomic<float> reverbGrDb{0.0f};
    std::atomic<bool> usingExternalKey{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClearSpaceProcessor)
};

} // namespace clearspace
