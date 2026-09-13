#pragma once

#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace clearspace
{

/** Clear Space — ducking delay + reverb.

    Phase 0: stereo pass-through with the full APVTS layout and a sidechain input bus.
    Buses: main in (mono or stereo) → main out (stereo); optional "Sidechain" input
    (mono or stereo, off by default) that Phase 1's ducker uses as the external key.
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
    double getTailLengthSeconds() const override { return 0.0; }

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

    static constexpr int mainInputBus = 0;
    static constexpr int sidechainBus = 1;
    static constexpr int mainOutputBus = 0;

private:
    static BusesProperties makeBuses();

    juce::AudioProcessorValueTreeState apvts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClearSpaceProcessor)
};

} // namespace clearspace
