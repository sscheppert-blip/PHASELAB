#pragma once

#include "shared/AnalyzerProcessor.h"
#include "ui/Panel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace phaselab
{

// Hosts the instrument panel at its design size and scales it as a whole with the window.
class AnalyzerEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AnalyzerEditor (AnalyzerProcessor&);

    void paint (juce::Graphics&) override {}
    void resized() override;

    ui::Panel& getPanel() noexcept { return panel; }

private:
    ui::Panel panel;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerEditor)
};

} // namespace phaselab
