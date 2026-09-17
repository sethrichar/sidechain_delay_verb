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

    Phase 1: input trim → key (internal or external sidechain) → two duckers → Routing
    (delay / reverb stand-ins, serial/parallel, levels, bypass, equal-power mix, output trim).
    Buses: main in (mono or stereo) → main out (stereo); optional "Sidechain" input
    (mono or stereo, off by default) used as the external key when `duckSource` = External.
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

    /** Meter feeds (dB of gain reduction, ≥ 0), written by the audio thread once per block
        with the block's peak. Poll from a juce::Timer; never block on them. */
    float getDelayGainReductionDb() const noexcept
    {
        return delayGrDb.load(std::memory_order_relaxed);
    }
    float getReverbGainReductionDb() const noexcept
    {
        return reverbGrDb.load(std::memory_order_relaxed);
    }
    /** True when the last block took its key from the external sidechain bus. */
    bool isUsingExternalKey() const noexcept
    {
        return usingExternalKey.load(std::memory_order_relaxed);
    }

    static constexpr int mainInputBus = 0;
    static constexpr int sidechainBus = 1;
    static constexpr int mainOutputBus = 0;

    /** Input trim ramp. */
    static constexpr float inputTrimSmoothingMs = 20.0f;

private:
    static BusesProperties makeBuses();

    void cacheParameterPointers();
    void updateFromParameters();
    void processChunk(const float* inL, const float* inR, const float* keyL, const float* keyR,
                      float* outL, float* outR, int numSamples);

    juce::AudioProcessorValueTreeState apvts;

    // DSP
    dsp::Ducker delayDucker, reverbDucker;
    dsp::Routing routing;
    dsp::standin::StandInDelay delayEffect;
    dsp::standin::StandInReverb reverbEffect;
    juce::LinearSmoothedValue<float> inputGain{1.0f};

    // Scratch (sized in prepareToPlay)
    juce::AudioBuffer<float> inBuf;   // post-trim stereo input
    juce::AudioBuffer<float> keyBuf;  // mono key
    juce::AudioBuffer<float> gainBuf; // ch0 delay duck gain, ch1 reverb duck gain
    int maxBlock = 0;

    // Meter feeds
    std::atomic<float> delayGrDb{0.0f};
    std::atomic<float> reverbGrDb{0.0f};
    std::atomic<bool> usingExternalKey{false};

    // Parameter atomics (owned by the APVTS)
    struct RawParams
    {
        std::atomic<float>* inputTrim = nullptr;
        std::atomic<float>* outputTrim = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* routing = nullptr;
        std::atomic<float>* duckSource = nullptr;
        std::atomic<float>* duckLink = nullptr;
        std::atomic<float>* delayBypass = nullptr;
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
            dsp::Ducker::Params read() const noexcept;
        } delayDuck, reverbDuck;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClearSpaceProcessor)
};

} // namespace clearspace
