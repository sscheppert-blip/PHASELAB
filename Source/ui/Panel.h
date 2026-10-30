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

    // For tests and snapshots: fetch the newest data right now; pick a graph view (0–2).
    void refresh();
    void showView (int index);
    void showHistory (int seconds);   // 30, 60, 90 or 0 for the whole song

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
    juce::TextButton historyButtons[4];       // 30 s, 60 s, 90 s, whole song
    juce::TextButton historyModeButtons[3];   // modes 0 correlation, 1 level, 2 loudness (shown loudness first)

    // History view, stored with the plugin state (a viewer convenience, not a host parameter)
    int historyMode() const;
    juce::ComboBox modeBox, slotBox, groupBox;
    juce::Slider cutoffSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider waveZoom { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };   // waveform window, ms
    juce::TextButton traceButtons[2];   // show A/B (L/R) in waveform and A/B spectrum
    juce::TextButton triggerButton;     // oscilloscope trigger for the waveform

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, slotAttachment, groupAttachment;
    std::unique_ptr<SliderAttachment> cutoffAttachment;

    View view = View::spectrum;
    int historySeconds = 60;                  // 0: whole song
    std::vector<History::Point> songPoints;   // copy of the engine's whole-song history
    int songVersion = -1;
    bool lastMono = false, lastAnalysing = true;

    // Newest data
    std::unique_ptr<EngineSnapshot> snapshot;
    BusLinkStatus link;
    int fullScore = -1, bandScore = -1;
    float meterLeft = 0.0f, meterRight = 0.0f, peakHoldLeft = 0.0f, peakHoldRight = 0.0f;
    float sendPeak = 0.0f;
    juce::uint32 gapShownUntil = 0;   // keep a gap notice readable for a moment
    juce::uint32 clipLeftUntil = 0, clipRightUntil = 0;   // clip LEDs stay lit for 3 s

    // The hint line changes at reading speed, not at measuring speed.
    void updateAdvice();
    Advice shownAdvice;
    juce::String shownKind, pendingKind;
    juce::uint32 shownSince = 0, pendingSince = 0, textUpdated = 0;
    bool shownIdle = true;   // the shown hint is a state message (no signal, link state): replace it at once

    // Areas computed in resized()
    juce::Rectangle<int> headerArea, warnArea, sourceArea, toolbarArea, spectrumArea, stripArea, readoutArea,
        waveTitleArea, waveArea, monitorArea, historyArea, adviceArea, footerArea, workArea;
    // Monitor column
    juce::Rectangle<int> monitorTitle, meterRect, scoreRect, bandRow, sliderRect, correlationRect, gonioRect,
        stereoFoot, valuesRect, resetRect, clipRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
};

} // namespace phaselab::ui
