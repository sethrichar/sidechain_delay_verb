#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace clearspace
{

/** Phase 0–5 editor: a generic auto-generated parameter panel so every parameter can be
    auditioned in the Standalone app or a DAW before the real UI lands in Phase 6, plus a
    one-line gain-reduction readout (polled at 30 Hz from the processor's atomics). */
class ClearSpaceEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ClearSpaceEditor(ClearSpaceProcessor& owner);
    ~ClearSpaceEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    ClearSpaceProcessor& owner;
    juce::GenericAudioProcessorEditor genericPanel;
    juce::Label meterLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClearSpaceEditor)
};

} // namespace clearspace
