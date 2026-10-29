#pragma once

#include "shared/AnalyzerProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace phaselab
{

// P1 panel: mono scores, the link (group, slot, send state) and in the Bus Analyzer the A/B
// comparison. The full instrument view (spectrum, waveforms, goniometer, history) follows in P3
// after the preview in docs/spec.
class AnalyzerEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AnalyzerEditor (AnalyzerProcessor&);
    ~AnalyzerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 640;
    static constexpr int channelHeight = 340;
    static constexpr int busHeight = 520;

private:
    void timerCallback() override;
    void updateVisibility();
    juce::Rectangle<int> scoreArea() const;
    void paintScores (juce::Graphics&, juce::Rectangle<int>);
    void paintSendOnly (juce::Graphics&, juce::Rectangle<int>);
    void paintLink (juce::Graphics&, int top);

    AnalyzerProcessor& processor;
    const bool isChannel;

    juce::TextButton monoButton { "MONO" };
    juce::ComboBox modeBox, slotBox, groupBox;
    juce::Slider cutoffSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, slotAttachment, groupAttachment;
    std::unique_ptr<SliderAttachment> cutoffAttachment;

    int shownFull = -2, shownBand = -2;
    float shownPeak = 0.0f;
    SendState shownSend = SendState::off;
    BusLinkStatus shownLink;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerEditor)
};

} // namespace phaselab
