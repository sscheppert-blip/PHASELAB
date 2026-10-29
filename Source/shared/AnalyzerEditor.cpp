#include "shared/AnalyzerEditor.h"

namespace phaselab
{

AnalyzerEditor::AnalyzerEditor (AnalyzerProcessor& p) : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);

    // Fixed layout, the window scales as a whole (60 % … 150 %).
    constexpr int w = ui::Panel::designWidth, h = ui::Panel::designHeight;
    setResizable (true, true);
    setResizeLimits (w * 6 / 10, h * 6 / 10, w * 3 / 2, h * 3 / 2);
    if (auto* fixedRatio = getConstrainer())
        fixedRatio->setFixedAspectRatio ((double) w / h);
    setSize (w * 9 / 10, h * 9 / 10);
}

void AnalyzerEditor::resized()
{
    const float scale = (float) getWidth() / (float) ui::Panel::designWidth;
    panel.setBounds (0, 0, ui::Panel::designWidth, ui::Panel::designHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));
}

} // namespace phaselab
