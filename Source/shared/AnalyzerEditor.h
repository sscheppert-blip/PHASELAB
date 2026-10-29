#pragma once

#include "shared/AnalyzerProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace phaselab
{

// P0 panel: shows the mono scores and the controls that exist so far. The full instrument view
// (spectrum, waveforms, goniometer, history) follows in P3 after the preview in docs/spec.
class AnalyzerEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AnalyzerEditor (AnalyzerProcessor&);
    ~AnalyzerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 560;
    static constexpr int designHeight = 320;

private:
    void timerCallback() override;
    void updateVisibility();

    AnalyzerProcessor& processor;
    const bool isChannel;

    juce::TextButton monoButton { "MONO" };
    juce::ComboBox modeBox, slotBox;
    juce::Slider cutoffSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, slotAttachment;
    std::unique_ptr<SliderAttachment> cutoffAttachment;

    int shownFull = -2, shownBand = -2;
    float shownPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerEditor)
};

} // namespace phaselab
