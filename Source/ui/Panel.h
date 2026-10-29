#pragma once

#include "shared/AnalyzerProcessor.h"
#include "ui/Advice.h"
#include "ui/Look.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace phaselab::ui
{

// The instrument view of both plugins at its design size (layout after the preview in docs/spec).
// The editor scales it as a whole.
class Panel final : public juce::Component, private juce::Timer
{
public:
    static constexpr int designWidth = 1100;
    static constexpr int designHeight = 780;

    explicit Panel (AnalyzerProcessor&);
    ~Panel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // For tests and snapshots: fetch the newest data right now.
    void refresh();

private:
    enum class View { spectrum, second, monoLoss };   // second: Phase A/B (bus) or Stereo M/S (channel)

    void timerCallback() override { refresh(); repaint(); }
    void layoutControls();
    void updateTexts();
    bool english() const { return processor.isEnglish(); }
    bool linkValid() const { return ! isChannel && link.state == LinkState::valid; }

    void paintHeader (juce::Graphics&);
    void paintSourceBar (juce::Graphics&, juce::Rectangle<int>);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<int>);
    void paintBandStrip (juce::Graphics&, juce::Rectangle<int>);
    void paintReadout (juce::Graphics&, juce::Rectangle<int>);
    void paintWaveform (juce::Graphics&, juce::Rectangle<int>);
    void paintMonitor (juce::Graphics&, juce::Rectangle<int>);
    void paintMeters (juce::Graphics&, juce::Rectangle<int>);
    void paintGoniometer (juce::Graphics&, juce::Rectangle<int>);
    void paintHistory (juce::Graphics&, juce::Rectangle<int>);
    void paintAdvice (juce::Graphics&, juce::Rectangle<int>);
    void paintFooter (juce::Graphics&, juce::Rectangle<int>);
    void paintSendOnly (juce::Graphics&, juce::Rectangle<int>);

    AnalyzerProcessor& processor;
    const bool isChannel;
    Look look;

    juce::TextButton monoButton { "MONO" }, languageButton, holdButton { "HOLD" }, resetButton;
    juce::TextButton viewButtons[3];
    juce::TextButton historyButtons[3];
    juce::ComboBox modeBox, slotBox, groupBox;
    juce::Slider cutoffSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, slotAttachment, groupAttachment;
    std::unique_ptr<SliderAttachment> cutoffAttachment;

    View view = View::spectrum;
    int historySeconds = 60;
    bool lastMono = false, lastAnalysing = true;

    // Newest data
    std::unique_ptr<EngineSnapshot> snapshot;
    BusLinkStatus link;
    int fullScore = -1, bandScore = -1;
    float meterLeft = 0.0f, meterRight = 0.0f, peakHoldLeft = 0.0f, peakHoldRight = 0.0f;
    float sendPeak = 0.0f;
    juce::uint32 gapShownUntil = 0;   // keep a gap notice readable for a moment

    // Areas computed in resized()
    juce::Rectangle<int> headerArea, warnArea, sourceArea, toolbarArea, spectrumArea, stripArea, readoutArea,
        waveTitleArea, waveArea, monitorArea, historyArea, adviceArea, footerArea, workArea;
    // Monitor column
    juce::Rectangle<int> monitorTitle, meterRect, scoreRect, bandRow, sliderRect, correlationRect, gonioRect,
        stereoFoot, valuesRect, resetRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
};

} // namespace phaselab::ui
