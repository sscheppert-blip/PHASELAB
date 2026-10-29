#include "shared/AnalyzerEditor.h"

namespace phaselab
{

namespace
{
    // Colours of the preview in docs/spec/PhaseLab_Konzept_Bus_und_Channel_Analyzer.html
    const juce::Colour panel { 0xff292f34 }, headTop { 0xff3b444a }, headBottom { 0xff2d353a },
                       bar { 0xff232a2f }, field { 0xff20292f }, line { 0xff46515a },
                       text { 0xffdce2e5 }, sub { 0xffaebbc2 }, teal { 0xff80c2c8 },
                       amber { 0xffe0b77a }, warnBack { 0xff4a3d27 }, warnText { 0xfff0d3a2 },
                       good { 0xff82c4ae };

    constexpr int headerHeight = 56, warnHeight = 26, footerHeight = 30, margin = 16;

    juce::String scoreText (int score) { return score < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : juce::String (score); }
}

AnalyzerEditor::AnalyzerEditor (AnalyzerProcessor& p)
    : AudioProcessorEditor (p), processor (p), isChannel (p.getRole() == Role::channel)
{
    setSize (designWidth, designHeight);

    monoButton.setClickingTogglesState (true);
    monoButton.setToggleState (processor.isMonoMonitor(), juce::dontSendNotification);
    monoButton.setTooltip ("Ausgang zum Abhören mono summieren");
    monoButton.setColour (juce::TextButton::buttonColourId, bar);
    monoButton.setColour (juce::TextButton::buttonOnColourId, amber);
    monoButton.setColour (juce::TextButton::textColourOffId, sub);
    monoButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1d2226));
    monoButton.onClick = [this]
    {
        processor.setMonoMonitor (monoButton.getToggleState());
        repaint();
    };
    addAndMakeVisible (monoButton);

    auto styleCombo = [this] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, bar);
        box.setColour (juce::ComboBox::outlineColourId, line);
        box.setColour (juce::ComboBox::textColourId, text);
        box.setColour (juce::ComboBox::arrowColourId, sub);
        addAndMakeVisible (box);
    };

    if (isChannel)
    {
        modeBox.addItemList ({ "Analyse", "Analyse + Senden", "Nur Senden" }, 1);
        slotBox.addItemList ({ "Slot A", "Slot B" }, 1);
        styleCombo (modeBox);
        styleCombo (slotBox);
        modeAttachment = std::make_unique<ComboAttachment> (processor.getState(), ids::mode, modeBox);
        slotAttachment = std::make_unique<ComboAttachment> (processor.getState(), ids::slot, slotBox);
        modeBox.onChange = [this] { updateVisibility(); repaint(); };
    }

    cutoffSlider.setColour (juce::Slider::trackColourId, teal);
    cutoffSlider.setColour (juce::Slider::backgroundColourId, line);
    cutoffSlider.setColour (juce::Slider::thumbColourId, teal);
    cutoffSlider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (cutoffSlider);
    cutoffAttachment = std::make_unique<SliderAttachment> (processor.getState(), ids::bandCutoff, cutoffSlider);

    updateVisibility();
    startTimerHz (15);
}

AnalyzerEditor::~AnalyzerEditor()
{
    stopTimer();
}

void AnalyzerEditor::updateVisibility()
{
    const bool analysing = processor.isAnalysing();
    cutoffSlider.setVisible (analysing);
    slotBox.setEnabled (processor.getMode() != ChannelMode::analyse);
}

void AnalyzerEditor::timerCallback()
{
    // The project can switch the mode or reset the mono monitor behind the editor's back.
    if (monoButton.getToggleState() != processor.isMonoMonitor())
        monoButton.setToggleState (processor.isMonoMonitor(), juce::dontSendNotification);
    if (cutoffSlider.isVisible() != processor.isAnalysing())
        updateVisibility();

    const int full = processor.getMeter().fullScore();
    const int band = processor.getMeter().bandScore();
    const float peak = juce::jmax (processor.takeInputPeak(), shownPeak * 0.8f);

    if (full != shownFull || band != shownBand || std::abs (peak - shownPeak) > 0.002f)
    {
        shownFull = full;
        shownBand = band;
        shownPeak = peak;
        repaint();
    }
}

void AnalyzerEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll (panel);

    // Header: word mark, component name
    auto header = bounds.withHeight (headerHeight);
    g.setGradientFill (juce::ColourGradient (headTop, 0.0f, 0.0f, headBottom, 0.0f, (float) headerHeight, false));
    g.fillRect (header);
    g.setColour (juce::Colour (0xff131b20));
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    g.setFont (juce::FontOptions (25.0f));
    g.setColour (text);
    g.drawText ("Phase", margin, 0, 80, headerHeight, juce::Justification::centredLeft);
    const int phaseWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "Phase");
    g.setColour (teal);
    g.drawText ("Lab", margin + phaseWidth, 0, 60, headerHeight, juce::Justification::centredLeft);
    const int markEnd = margin + phaseWidth
                      + juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "Lab") + 14;
    g.setColour (line);
    g.fillRect (markEnd, 14, 1, headerHeight - 28);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (sub);
    g.drawText (isChannel ? "CHANNEL ANALYZER" : "BUS ANALYZER", markEnd + 14, 0, 200, headerHeight,
                juce::Justification::centredLeft);

    const bool mono = processor.isMonoMonitor();
    if (mono)
    {
        g.setColour (warnBack);
        g.fillRect (0, headerHeight, getWidth(), warnHeight);
        g.setColour (warnText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (juce::CharPointer_UTF8 ("MONO-ABH\xc3\x96REN AKTIV \xc2\xb7 Ausgang mono, die Analyse misst das Original. Vor dem Export aus!"),
                    margin, headerHeight, getWidth() - 2 * margin, warnHeight, juce::Justification::centredLeft);
    }

    // Source / status row
    const int rowY = headerHeight + warnHeight + 8;
    g.setFont (juce::FontOptions (13.5f));
    g.setColour (sub);
    juce::String status;
    if (isChannel)
    {
        const auto m = processor.getMode();
        status = m == ChannelMode::analyse ? juce::String ("LOKAL")
                                           : juce::String ("Senden an Slot ") + (slotBox.getSelectedId() == 2 ? "B" : "A")
                                                 + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 Transport folgt in P1"));
        g.drawText (status, 310, rowY, getWidth() - 310 - margin, 30, juce::Justification::centredRight);
    }
    else
    {
        g.drawText (juce::CharPointer_UTF8 ("Quellen A/B: nicht verbunden \xc2\xb7 Link-Transport folgt in P1"),
                    margin, rowY, getWidth() - 2 * margin, 30, juce::Justification::centredLeft);
    }

    const int bodyY = rowY + 42;
    const int bodyBottom = getHeight() - footerHeight - 12;

    if (! processor.isAnalysing())
    {
        // "Nur Senden": slot letter, status, input activity – no analysis (D-21)
        auto box = juce::Rectangle<int> (margin, bodyY, getWidth() - 2 * margin, bodyBottom - bodyY);
        g.setColour (field);
        g.fillRect (box);
        const bool slotB = slotBox.getSelectedId() == 2;
        auto letter = juce::Rectangle<int> (box.getX() + 18, box.getY() + 18, 72, 72);
        g.setColour (slotB ? juce::Colour (0xffc5a378) : teal);
        g.drawRect (letter, 2);
        g.setFont (juce::FontOptions (40.0f));
        g.drawText (slotB ? "B" : "A", letter, juce::Justification::centred);

        g.setColour (text);
        g.setFont (juce::FontOptions (20.0f));
        g.drawText (juce::String ("Nur Senden ") + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 Slot ")) + (slotB ? "B" : "A"),
                    letter.getRight() + 18, letter.getY() + 6, 300, 28, juce::Justification::centredLeft);
        g.setColour (sub);
        g.setFont (juce::FontOptions (13.5f));
        g.drawFittedText (juce::CharPointer_UTF8 ("Analyse und Anzeigen sind aus, um CPU zu sparen. Das Audio l\xc3\xa4uft unver\xc3\xa4ndert durch."),
                          letter.getRight() + 18, letter.getY() + 36, box.getRight() - letter.getRight() - 30, 40,
                          juce::Justification::topLeft, 2, 1.0f);

        auto meterBar = juce::Rectangle<float> ((float) box.getX() + 18, (float) letter.getBottom() + 22, 300.0f, 8.0f);
        g.setColour (juce::Colour (0xff334147));
        g.fillRect (meterBar);
        const float level = juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (shownPeak, -60.0f) / 60.0f);
        g.setColour (good);
        g.fillRect (meterBar.withWidth (meterBar.getWidth() * level));
        g.setColour (sub);
        g.drawText (shownPeak > 0.001f ? "Signal vorhanden" : "Kein Signal", (int) meterBar.getX(),
                    (int) meterBar.getBottom() + 4, 200, 18, juce::Justification::centredLeft);
    }
    else
    {
        // Full-band and band mono score (D-07, D-22)
        const int gap = 12;
        const int half = (getWidth() - 2 * margin - gap) / 2;
        auto left = juce::Rectangle<int> (margin, bodyY, half, bodyBottom - bodyY);
        auto right = left.withX (left.getRight() + gap);

        for (auto r : { left, right })
        {
            g.setColour (field);
            g.fillRect (r);
        }

        g.setColour (sub);
        g.setFont (juce::FontOptions (13.5f));
        g.drawText (juce::CharPointer_UTF8 ("MONO-SCORE \xc2\xb7 Vollband"), left.reduced (14, 10).removeFromTop (20),
                    juce::Justification::centredLeft);
        g.drawText ("MONO-SCORE unter " + juce::String (juce::roundToInt (cutoffSlider.getValue())) + " Hz",
                    right.reduced (14, 10).removeFromTop (20), juce::Justification::centredLeft);

        auto drawScore = [&g] (juce::Rectangle<int> area, int score, float size)
        {
            g.setColour (text);
            g.setFont (juce::FontOptions (size));
            g.drawText (scoreText (score), area, juce::Justification::centredLeft);
            if (score >= 0)
            {
                const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), scoreText (score));
                g.setColour (sub);
                g.setFont (juce::FontOptions (15.0f));
                g.drawText (" /100", area.withTrimmedLeft (w), juce::Justification::centredLeft);
            }
        };
        drawScore ({ left.getX() + 14, left.getY() + 34, left.getWidth() - 28, 60 }, shownFull, 52.0f);
        drawScore ({ right.getX() + 14, right.getY() + 34, right.getWidth() - 28, 44 }, shownBand, 36.0f);

        g.setColour (sub);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("40 Hz", right.getX() + 14, right.getBottom() - 24, 60, 16, juce::Justification::centredLeft);
        g.drawText ("500 Hz", right.getRight() - 74, right.getBottom() - 24, 60, 16, juce::Justification::centredRight);
        g.drawText (shownFull < 0 ? "Zu wenig Signal" : juce::String (juce::CharPointer_UTF8 ("Fenster 400 ms \xc2\xb7 Formel-Entwurf")),
                    left.getX() + 14, left.getBottom() - 24, left.getWidth() - 28, 16, juce::Justification::centredLeft);
    }

    // Footer
    auto footer = bounds.withTop (getHeight() - footerHeight);
    g.setColour (juce::Colour (0xff2c343a));
    g.fillRect (footer);
    g.setColour (line);
    g.drawHorizontalLine (footer.getY(), 0.0f, (float) getWidth());
    g.setColour (sub);
    g.setFont (juce::FontOptions (12.5f));
    const double rate = processor.getSampleRate();
    g.drawText (rate > 0 ? juce::String (rate / 1000.0, 1) + juce::String (juce::CharPointer_UTF8 (" kHz \xc2\xb7 ")) + juce::String (processor.getBlockSize()) + " Samples"
                         : juce::String ("nicht aktiv"),
                footer.reduced (margin, 0), juce::Justification::centredLeft);
    const juce::String running = mono ? juce::String (juce::CharPointer_UTF8 ("MONO-ABH\xc3\x96REN \xc2\xb7 Ausgang mono"))
                               : processor.isAnalysing() ? juce::String (juce::CharPointer_UTF8 ("ANALYSE AKTIV \xc2\xb7 Audio unver\xc3\xa4ndert"))
                                                         : juce::String (juce::CharPointer_UTF8 ("ANALYSE AUS \xc2\xb7 Audio unver\xc3\xa4ndert"));
    g.setColour (mono ? amber : sub);
    g.drawText (running, footer, juce::Justification::centred);
    g.setColour (sub);
    g.drawText ("P0 " PHASELAB_VERSION_STRING, footer.reduced (margin, 0), juce::Justification::centredRight);
}

void AnalyzerEditor::resized()
{
    monoButton.setBounds (getWidth() - margin - 72, (headerHeight - 28) / 2, 72, 28);

    const int rowY = headerHeight + warnHeight + 8;
    modeBox.setBounds (margin, rowY, 190, 30);
    slotBox.setBounds (margin + 200, rowY, 96, 30);

    const int bodyBottom = getHeight() - footerHeight - 12;
    const int gap = 12;
    const int half = (getWidth() - 2 * margin - gap) / 2;
    cutoffSlider.setBounds (margin + half + gap + 10, bodyBottom - 52, half - 20, 24);
}

} // namespace phaselab
