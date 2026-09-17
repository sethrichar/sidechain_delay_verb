#include "PluginEditor.h"

namespace clearspace
{

namespace
{
constexpr int meterRefreshHz = 30;
constexpr int meterRowHeight = 24;
} // namespace

ClearSpaceEditor::ClearSpaceEditor(ClearSpaceProcessor& p)
    : AudioProcessorEditor(p)
    , owner(p)
    , genericPanel(p)
{
    addAndMakeVisible(genericPanel);

    meterLabel.setJustificationType(juce::Justification::centredLeft);
    meterLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(meterLabel);

    setResizable(true, true);
    setResizeLimits(480, 400, 1600, 1600);
    setSize(640, 760);
    startTimerHz(meterRefreshHz);
    timerCallback();
}

ClearSpaceEditor::~ClearSpaceEditor()
{
    stopTimer();
}

void ClearSpaceEditor::timerCallback()
{
    // Atomics only — the audio thread never touches this component.
    const auto delayGr = owner.getDelayGainReductionDb();
    const auto reverbGr = owner.getReverbGainReductionDb();
    meterLabel.setText("Duck GR   delay " + juce::String(delayGr, 1) + " dB   reverb " +
                           juce::String(reverbGr, 1) +
                           " dB   key: " + (owner.isUsingExternalKey() ? "external" : "internal"),
                       juce::dontSendNotification);
}

void ClearSpaceEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ClearSpaceEditor::resized()
{
    auto area = getLocalBounds();
    meterLabel.setBounds(area.removeFromTop(meterRowHeight).reduced(8, 0));
    genericPanel.setBounds(area);
}

} // namespace clearspace
