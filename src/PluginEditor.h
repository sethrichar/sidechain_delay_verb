#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace clearspace
{

/** Phase 0–5 editor: a generic auto-generated parameter panel so every parameter can be
    auditioned in the Standalone app or a DAW before the real UI lands in Phase 6. */
class ClearSpaceEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ClearSpaceEditor(ClearSpaceProcessor& owner);
    ~ClearSpaceEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::GenericAudioProcessorEditor genericPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClearSpaceEditor)
};

} // namespace clearspace
