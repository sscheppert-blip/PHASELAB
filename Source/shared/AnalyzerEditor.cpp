#include "shared/AnalyzerEditor.h"

namespace phaselab
{

namespace
{
    // Colours of the preview in docs/spec/PhaseLab_Konzept_Bus_und_Channel_Analyzer.html
    const juce::Colour panel { 0xff292f34 }, headTop { 0xff3b444a }, headBottom { 0xff2d353a },
                       bar { 0xff232a2f }, field { 0xff20292f }, line { 0xff46515a },
                       text { 0xffdce2e5 }, sub { 0xffaebbc2 }, teal { 0xff80c2c8 }, sand { 0xffc5a378 },
                       amber { 0xffe0b77a }, warnBack { 0xff4a3d27 }, warnText { 0xfff0d3a2 },
                       good { 0xff82c4ae }, bad { 0xffc8766a };

    constexpr int headerHeight = 56, warnHeight = 26, footerHeight = 30, margin = 16, gap = 12;
    constexpr int rowY = headerHeight + warnHeight + 8;

    juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
    juce::String dash() { return utf8 ("\xe2\x80\x93"); }
    juce::String scoreText (int score) { return score < 0 ? dash() : juce::String (score); }

    juce::String decimal (double value, int places, bool sign)
    {
        auto s = juce::String (std::abs (value), places).replace (".", ",");
        if (value < 0.0)
            return utf8 ("\xe2\x88\x92") + s;
        return sign ? "+" + s : s;
    }

    juce::String stateText (LinkState s)
    {
        switch (s)
        {
            case LinkState::disconnected: return utf8 ("Quelle fehlt \xc2\xb7 Channel Analyzer auf Senden, gleiche Gruppe");
            case LinkState::stale:        return utf8 ("Quelle liefert keine Daten (Wiedergabe gestoppt?)");
            case LinkState::offline:      return utf8 ("Offline-Rendern \xc2\xb7 A/B-Vergleich gesperrt");
            case LinkState::noHostTime:   return utf8 ("Wiedergabe starten \xc2\xb7 ohne Timeline kein A/B-Vergleich");
            case LinkState::rateMismatch: return utf8 ("Samplerate von A und B verschieden");
            case LinkState::acquiring:    return utf8 ("Synchronisiere \xe2\x80\xa6");
            case LinkState::gap:          return utf8 ("L\xc3\xbc" "cke in den Daten \xc2\xb7 Vergleich ausgesetzt");
            case LinkState::valid:        return utf8 ("SYNC \xc2\xb7 Vergleich g\xc3\xbcltig");
        }
        return {};
    }

    juce::String hintText (const ABResult& r, double rate)
    {
        if (! r.enoughSignal)
            return "Zu wenig Signal auf A oder B.";
        if (! r.clearOffset)
            return utf8 ("Kein eindeutiger Versatz \xc2\xb7 A und B \xc3\xa4hneln sich wenig (bei Kick und Bass normal).");

        const int n = std::abs (r.offsetSamples);
        const auto ms = decimal (1000.0 * n / rate, 2, false);
        juce::String hint;
        if (n == 0)
            hint = "Kein Versatz zwischen A und B.";
        else if (r.offsetSamples > 0)
            hint = "B liegt " + juce::String (n) + " Samples (" + ms + " ms) hinter A " + utf8 ("\xe2\x80\x93")
                 + " Spur B um " + juce::String (n) + " Samples vorziehen und erneut " + utf8 ("pr\xc3\xbc" "fen.");
        else
            hint = "B liegt " + juce::String (n) + " Samples (" + ms + " ms) vor A " + utf8 ("\xe2\x80\x93")
                 + " Spur B um " + juce::String (n) + utf8 (" Samples sp\xc3\xa4ter setzen und erneut pr\xc3\xbc" "fen.");

        if (r.polarityHint)
            hint << utf8 (" Polarit\xc3\xa4t von B umkehren w\xc3\xbcrde die Korrelation von ") << decimal (r.offsetCorrelation, 2, true)
                 << " auf " << decimal (-r.offsetCorrelation, 2, true) << " bringen.";
        return hint;
    }
}

AnalyzerEditor::AnalyzerEditor (AnalyzerProcessor& p)
    : AudioProcessorEditor (p), processor (p), isChannel (p.getRole() == Role::channel)
{
    setSize (designWidth, isChannel ? channelHeight : busHeight);

    monoButton.setClickingTogglesState (true);
    monoButton.setToggleState (processor.isMonoMonitor(), juce::dontSendNotification);
    monoButton.setTooltip (utf8 ("Ausgang zum Abh\xc3\xb6ren mono summieren"));
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
        box.onChange = [this] { updateVisibility(); repaint(); };
        addAndMakeVisible (box);
    };

    groupBox.addItemList ({ "Gruppe 1", "Gruppe 2", "Gruppe 3", "Gruppe 4" }, 1);
    styleCombo (groupBox);
    groupAttachment = std::make_unique<ComboAttachment> (processor.getState(), ids::group, groupBox);

    if (isChannel)
    {
        modeBox.addItemList ({ "Analyse", "Analyse + Senden", "Nur Senden" }, 1);
        slotBox.addItemList ({ "Slot A", "Slot B" }, 1);
        styleCombo (modeBox);
        styleCombo (slotBox);
        modeAttachment = std::make_unique<ComboAttachment> (processor.getState(), ids::mode, modeBox);
        slotAttachment = std::make_unique<ComboAttachment> (processor.getState(), ids::slot, slotBox);
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
    cutoffSlider.setVisible (processor.isAnalysing());
    slotBox.setEnabled (processor.isSending());
    groupBox.setEnabled (! isChannel || processor.isSending());
}

void AnalyzerEditor::timerCallback()
{
    // The project can switch the mode or reset the mono monitor behind the editor's back.
    if (monoButton.getToggleState() != processor.isMonoMonitor())
        monoButton.setToggleState (processor.isMonoMonitor(), juce::dontSendNotification);
    if (cutoffSlider.isVisible() != processor.isAnalysing())
        updateVisibility();

    shownFull = processor.getMeter().fullScore();
    shownBand = processor.getMeter().bandScore();
    shownPeak = juce::jmax (processor.takeInputPeak(), shownPeak * 0.8f);
    shownSend = processor.getSendState();
    if (! isChannel)
        shownLink = processor.getLinkStatus();
    repaint();
}

juce::Rectangle<int> AnalyzerEditor::scoreArea() const
{
    const int top = isChannel ? rowY + 42 : rowY + 42 + 62 + 168;
    return { margin, top, getWidth() - 2 * margin, getHeight() - footerHeight - 12 - top };
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
    const int markEnd = margin + phaseWidth + juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "Lab") + 14;
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
        g.drawText (utf8 ("MONO-ABH\xc3\x96REN AKTIV \xc2\xb7 Ausgang mono, die Analyse misst das Original. Vor dem Export aus!"),
                    margin, headerHeight, getWidth() - 2 * margin, warnHeight, juce::Justification::centredLeft);
    }

    // Status of the link in the control row
    g.setFont (juce::FontOptions (13.5f));
    if (isChannel)
    {
        juce::String status;
        juce::Colour colour = sub;
        const auto slotName = juce::String (processor.getSlot() == 0 ? "A" : "B");
        switch (shownSend)
        {
            case SendState::off:         status = "LOKAL"; break;
            case SendState::sending:     status = "Sendet an " + slotName; colour = good; break;
            case SendState::busy:        status = "Slot " + slotName + " belegt"; colour = amber; break;
            case SendState::unavailable: status = utf8 ("Verbinde \xe2\x80\xa6"); break;
        }
        if (processor.isSending() && shownSend == SendState::off)
            status = utf8 ("Wartet auf Audio \xe2\x80\xa6");
        g.setColour (colour);
        g.drawText (status, 420, rowY, getWidth() - 420 - margin, 30, juce::Justification::centredRight);
    }
    else
    {
        const bool valid = shownLink.state == LinkState::valid;
        g.setColour (valid ? good : sub);
        g.drawText (stateText (shownLink.state), 160, rowY, getWidth() - 160 - margin, 30, juce::Justification::centredRight);
        paintLink (g, rowY + 42);
    }

    if (processor.isAnalysing())
        paintScores (g, scoreArea());
    else
        paintSendOnly (g, scoreArea());

    // Footer
    auto footer = bounds.withTop (getHeight() - footerHeight);
    g.setColour (juce::Colour (0xff2c343a));
    g.fillRect (footer);
    g.setColour (line);
    g.drawHorizontalLine (footer.getY(), 0.0f, (float) getWidth());
    g.setColour (sub);
    g.setFont (juce::FontOptions (12.5f));
    const double rate = processor.getSampleRate();
    g.drawText (rate > 0 ? juce::String (rate / 1000.0, 1) + utf8 (" kHz \xc2\xb7 ") + juce::String (processor.getBlockSize()) + " Samples"
                         : juce::String ("nicht aktiv"),
                footer.reduced (margin, 0), juce::Justification::centredLeft);
    const juce::String running = mono ? utf8 ("MONO-ABH\xc3\x96REN \xc2\xb7 Ausgang mono")
                               : processor.isAnalysing() ? utf8 ("ANALYSE AKTIV \xc2\xb7 Audio unver\xc3\xa4ndert")
                                                         : utf8 ("ANALYSE AUS \xc2\xb7 Audio unver\xc3\xa4ndert");
    g.setColour (mono ? amber : sub);
    g.drawText (running, footer, juce::Justification::centred);
    g.setColour (sub);
    g.drawText ("P1 " PHASELAB_VERSION_STRING, footer.reduced (margin, 0), juce::Justification::centredRight);
}

void AnalyzerEditor::paintLink (juce::Graphics& g, int top)
{
    // Sources A and B
    const int half = (getWidth() - 2 * margin - gap) / 2;
    for (int i = 0; i < 2; ++i)
    {
        const auto& source = i == 0 ? shownLink.a : shownLink.b;
        auto card = juce::Rectangle<int> (margin + i * (half + gap), top, half, 50);
        g.setColour (field);
        g.fillRect (card);

        auto tag = juce::Rectangle<int> (card.getX() + 12, card.getY() + 12, 26, 26);
        g.setColour (i == 0 ? teal : sand);
        g.drawRect (tag, 1);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (i == 0 ? "A" : "B", tag, juce::Justification::centred);

        g.setColour (source.connected ? text : sub);
        g.setFont (juce::FontOptions (16.0f));
        const juce::String name = ! source.connected ? dash()
                                : source.name.empty() ? juce::String ("Channel Analyzer")
                                                      : juce::String (juce::CharPointer_UTF8 (source.name.c_str()));
        g.drawText (name, tag.getRight() + 10, card.getY() + 6, card.getWidth() - 60, 22, juce::Justification::centredLeft);
        g.setColour (! source.connected ? sub : source.stale ? amber : good);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (! source.connected ? "fehlt" : source.stale ? "liefert nichts" : "verbunden",
                    tag.getRight() + 10, card.getY() + 26, card.getWidth() - 60, 18, juce::Justification::centredLeft);
    }

    // A/B comparison
    const bool valid = shownLink.state == LinkState::valid;
    const auto& r = shownLink.result;
    auto left = juce::Rectangle<int> (margin, top + 62, half, 104);
    auto right = left.withX (left.getRight() + gap);
    g.setColour (field);
    g.fillRect (left);
    g.fillRect (right);

    g.setColour (sub);
    g.setFont (juce::FontOptions (13.5f));
    g.drawText (utf8 ("A \xe2\x86\x94 B \xc2\xb7 Korrelation"), left.getX() + 14, left.getY() + 10, half - 28, 20, juce::Justification::centredLeft);
    g.drawText ("A+B addieren sich unter " + juce::String (juce::roundToInt (cutoffSlider.getValue())) + " Hz",
                right.getX() + 14, right.getY() + 10, half - 28, 20, juce::Justification::centredLeft);

    g.setColour (text);
    g.setFont (juce::FontOptions (36.0f));
    g.drawText (valid && r.enoughSignal ? decimal (r.correlation, 2, true) : dash(),
                left.getX() + 14, left.getY() + 32, half - 28, 44, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (13.5f));
    g.setColour (sub);
    juce::String offset = "Versatz " + dash();
    if (valid && r.enoughSignal && r.clearOffset && shownLink.sampleRate > 0)
        offset = "Versatz " + juce::String (r.offsetSamples > 0 ? "+" : "") + juce::String (r.offsetSamples) + " Samples ("
               + decimal (1000.0 * r.offsetSamples / shownLink.sampleRate, 2, true) + " ms)";
    g.drawText (offset, left.getX() + 14, left.getY() + 78, half - 28, 20, juce::Justification::centredLeft);

    g.setColour (text);
    g.setFont (juce::FontOptions (36.0f));
    const juce::String sum = valid ? scoreText (r.sumScore) : dash();
    g.drawText (sum, right.getX() + 14, right.getY() + 32, half - 28, 44, juce::Justification::centredLeft);
    if (valid && r.sumScore >= 0)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), sum);
        g.setColour (sub);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (" /100", right.getX() + 14 + w, right.getY() + 32, 80, 44, juce::Justification::centredLeft);
    }
    g.setColour (sub);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("gleicher Regler wie der Band-Score", right.getX() + 14, right.getY() + 78, half - 28, 20, juce::Justification::centredLeft);

    // Hint (D-23): advice only, nothing is changed in the audio
    auto hint = juce::Rectangle<int> (margin, top + 62 + 110, getWidth() - 2 * margin, 46);
    g.setColour (juce::Colour (0xff333331));
    g.fillRect (hint);
    g.setColour (juce::Colour (0xffd1c9b9));
    g.setFont (juce::FontOptions (13.5f));
    g.drawFittedText (valid ? hintText (r, shownLink.sampleRate) : stateText (shownLink.state),
                      hint.reduced (14, 6), juce::Justification::centredLeft, 2, 1.0f);
}

void AnalyzerEditor::paintScores (juce::Graphics& g, juce::Rectangle<int> area)
{
    // Full-band and band mono score of this plugin's own input (D-07, D-22)
    const int half = (area.getWidth() - gap) / 2;
    auto left = area.withWidth (half);
    auto right = left.withX (left.getRight() + gap);
    g.setColour (field);
    g.fillRect (left);
    g.fillRect (right);

    const juce::String own = isChannel ? "" : utf8 (" \xc2\xb7 Bus-Eingang");
    g.setColour (sub);
    g.setFont (juce::FontOptions (13.5f));
    g.drawText (utf8 ("MONO-SCORE \xc2\xb7 Vollband") + own, left.reduced (14, 10).removeFromTop (20), juce::Justification::centredLeft);
    g.drawText ("MONO-SCORE unter " + juce::String (juce::roundToInt (cutoffSlider.getValue())) + " Hz",
                right.reduced (14, 10).removeFromTop (20), juce::Justification::centredLeft);

    const bool tall = area.getHeight() > 150;
    auto drawScore = [&g] (juce::Rectangle<int> box, int score, float size)
    {
        g.setColour (text);
        g.setFont (juce::FontOptions (size));
        g.drawText (scoreText (score), box, juce::Justification::centredLeft);
        if (score >= 0)
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), scoreText (score));
            g.setColour (sub);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText (" /100", box.withTrimmedLeft (w), juce::Justification::centredLeft);
        }
    };
    drawScore ({ left.getX() + 14, left.getY() + 32, left.getWidth() - 28, tall ? 60 : 44 }, shownFull, tall ? 52.0f : 38.0f);
    drawScore ({ right.getX() + 14, right.getY() + 32, right.getWidth() - 28, tall ? 44 : 36 }, shownBand, tall ? 36.0f : 30.0f);

    g.setColour (sub);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("40 Hz", right.getX() + 14, right.getBottom() - 22, 60, 16, juce::Justification::centredLeft);
    g.drawText ("500 Hz", right.getRight() - 74, right.getBottom() - 22, 60, 16, juce::Justification::centredRight);
    g.drawText (shownFull < 0 ? juce::String ("Zu wenig Signal") : utf8 ("Fenster 400 ms \xc2\xb7 Formel-Entwurf"),
                left.getX() + 14, left.getBottom() - 22, left.getWidth() - 28, 16, juce::Justification::centredLeft);
}

void AnalyzerEditor::paintSendOnly (juce::Graphics& g, juce::Rectangle<int> box)
{
    // "Nur Senden": slot letter, status, input activity – no analysis (D-21)
    g.setColour (field);
    g.fillRect (box);
    const bool slotB = processor.getSlot() == 1;
    auto letter = juce::Rectangle<int> (box.getX() + 18, box.getY() + 18, 72, 72);
    g.setColour (slotB ? sand : teal);
    g.drawRect (letter, 2);
    g.setFont (juce::FontOptions (40.0f));
    g.drawText (slotB ? "B" : "A", letter, juce::Justification::centred);

    const auto name = processor.getTrackName();
    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f));
    g.drawText ((name.isEmpty() ? juce::String ("Nur Senden") : name) + utf8 (" \xc2\xb7 Gruppe ") + juce::String (processor.getGroup() + 1)
                    + utf8 (" \xc2\xb7 Slot ") + (slotB ? "B" : "A"),
                letter.getRight() + 18, letter.getY() + 6, box.getRight() - letter.getRight() - 30, 28, juce::Justification::centredLeft);
    g.setColour (sub);
    g.setFont (juce::FontOptions (13.5f));
    g.drawFittedText (utf8 ("Analyse und Anzeigen sind aus, um CPU zu sparen. Das Audio l\xc3\xa4uft unver\xc3\xa4ndert durch."),
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

void AnalyzerEditor::resized()
{
    monoButton.setBounds (getWidth() - margin - 72, (headerHeight - 28) / 2, 72, 28);

    if (isChannel)
    {
        modeBox.setBounds (margin, rowY, 170, 30);
        slotBox.setBounds (margin + 180, rowY, 96, 30);
        groupBox.setBounds (margin + 286, rowY, 116, 30);
    }
    else
    {
        groupBox.setBounds (margin, rowY, 130, 30);
    }

    const auto area = scoreArea();
    const int half = (area.getWidth() - gap) / 2;
    cutoffSlider.setBounds (area.getX() + half + gap + 10, area.getBottom() - 48, half - 20, 24);
}

} // namespace phaselab
