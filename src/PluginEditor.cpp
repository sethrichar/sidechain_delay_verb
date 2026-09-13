#include "PluginEditor.h"

namespace clearspace
{

ClearSpaceEditor::ClearSpaceEditor(ClearSpaceProcessor& owner)
    : AudioProcessorEditor(owner)
    , genericPanel(owner)
{
    addAndMakeVisible(genericPanel);
    setResizable(true, true);
    setResizeLimits(480, 400, 1600, 1600);
    setSize(640, 760);
}

void ClearSpaceEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ClearSpaceEditor::resized()
{
    genericPanel.setBounds(getLocalBounds());
}

} // namespace clearspace
