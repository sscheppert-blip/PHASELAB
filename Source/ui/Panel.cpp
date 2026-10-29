#include "ui/Panel.h"

#include <cmath>

namespace phaselab::ui
{

namespace c = colours;

namespace
{
    constexpr int margin = 16;

    juce::Font font (float size) { return juce::FontOptions (size); }

    double xOfFrequency (double f, juce::Rectangle<float> r)
    {
        return r.getX() + std::log (f / 20.0) / std::log (1000.0) * r.getWidth();
    }

    juce::String levelText (double v, bool en, int places = 1)
    {
        return v <= noLevel + 1.0 ? juce::String::fromUTF8 ("–") : number (v, places, en);
    }

    void drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> r, bool labels)
    {
        static const double grid[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        for (double f : grid)
        {
            const float x = (float) xOfFrequency (f, r);
            g.setColour (c::grid);
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            if (labels)
            {
                g.setColour (c::sub);
                g.setFont (font (12.5f));
                const auto text = f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f);
                const auto box = f == 20 ? juce::Rectangle<float> (x, r.getBottom() + 3, 48, 16)
                               : f == 20000 ? juce::Rectangle<float> (x - 48, r.getBottom() + 3, 48, 16)
                                            : juce::Rectangle<float> (x - 24, r.getBottom() + 3, 48, 16);
                g.drawText (text, box, f == 20 ? juce::Justification::centredLeft : f == 20000 ? juce::Justification::centredRight : juce::Justification::centred);
            }
        }
    }

    juce::Path curveOf (const std::array<float, displayPoints>& db, juce::Rectangle<float> r, float floorDb)
    {
        juce::Path p;
        for (int i = 0; i < displayPoints; ++i)
        {
            const float x = (float) xOfFrequency (displayFrequency (i), r);
            const float v = juce::jlimit (floorDb, 0.0f, db[(size_t) i]);
            const float y = r.getY() + (v / floorDb) * r.getHeight();
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        return p;
    }

    void drawCurve (juce::Graphics& g, const std::array<float, displayPoints>& db, juce::Rectangle<float> r,
                    juce::Colour colour, float floorDb, bool fill)
    {
        auto p = curveOf (db, r, floorDb);
        if (fill)
        {
            auto area = p;
            area.lineTo (r.getRight(), r.getBottom());
            area.lineTo (r.getX(), r.getBottom());
            area.closeSubPath();
            g.setColour (colour.withAlpha (0.12f));
            g.fillPath (area);
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Colour correlationColour (double v)
    {
        v = juce::jlimit (-1.0, 1.0, v);
        if (v < 0.0)
        {
            const double k = -v;
            return juce::Colour::fromRGB (200, (juce::uint8) (150 - 60 * k), (juce::uint8) (110 - 30 * k));
        }
        return juce::Colour::fromRGB ((juce::uint8) (200 - 70 * v), (juce::uint8) (150 + 46 * v), (juce::uint8) (110 + 64 * v));
    }
}

Panel::Panel (AnalyzerProcessor& p)
    : processor (p), isChannel (p.getRole() == Role::channel), snapshot (std::make_unique<EngineSnapshot>())
{
    setLookAndFeel (&look);
    setSize (designWidth, designHeight);

    monoButton.setClickingTogglesState (true);
    monoButton.getProperties().set ("amber", true);
    monoButton.setColour (juce::TextButton::buttonOnColourId, c::amber);
    monoButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1d2226));
    monoButton.onClick = [this] { processor.setMonoMonitor (monoButton.getToggleState()); layoutControls(); repaint(); };
    addAndMakeVisible (monoButton);

    languageButton.onClick = [this] { processor.setEnglish (! english()); updateTexts(); repaint(); };
    addAndMakeVisible (languageButton);

    holdButton.setClickingTogglesState (true);
    addAndMakeVisible (holdButton);

    resetButton.onClick = [this] { processor.getEngine().requestReset(); peakHoldLeft = peakHoldRight = 0.0f; };
    addAndMakeVisible (resetButton);

    for (int i = 0; i < 3; ++i)
    {
        viewButtons[i].setRadioGroupId (1);
        viewButtons[i].setClickingTogglesState (true);
        viewButtons[i].onClick = [this, i] { view = (View) i; repaint(); };
        addAndMakeVisible (viewButtons[i]);

        const int secs = 30 * (i + 1);
        historyButtons[i].setButtonText (juce::String (secs) + " s");
        historyButtons[i].setRadioGroupId (2);
        historyButtons[i].setClickingTogglesState (true);
        historyButtons[i].onClick = [this, secs] { historySeconds = secs; repaint(); };
        addAndMakeVisible (historyButtons[i]);
    }
    viewButtons[0].setToggleState (true, juce::dontSendNotification);
    historyButtons[1].setToggleState (true, juce::dontSendNotification);

    auto addCombo = [this] (juce::ComboBox& box)
    {
        box.onChange = [this] { layoutControls(); repaint(); };
        addAndMakeVisible (box);
    };
    groupBox.addItemList ({ "1", "2", "3", "4" }, 1);
    addCombo (groupBox);
    groupAttachment = std::make_unique<ComboAttachment> (processor.getState(), ids::group, groupBox);
    if (isChannel)
    {
        modeBox.addItemList ({ "Analyse", "Analyse + Senden", "Nur Senden" }, 1);
        slotBox.addItemList ({ "Slot A", "Slot B" }, 1);
        addCombo (modeBox);
        addCombo (slotBox);
        modeAttachment = std::make_unique<ComboAttachment> (processor.getState(), ids::mode, modeBox);
        slotAttachment = std::make_unique<ComboAttachment> (processor.getState(), ids::slot, slotBox);
    }

    cutoffSlider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (cutoffSlider);
    cutoffAttachment = std::make_unique<SliderAttachment> (processor.getState(), ids::bandCutoff, cutoffSlider);

    processor.setDisplayActive (true);
    updateTexts();
    refresh();
    startTimerHz (20);
}

Panel::~Panel()
{
    stopTimer();
    processor.setDisplayActive (false);
    setLookAndFeel (nullptr);
}

void Panel::updateTexts()
{
    const bool en = english();
    languageButton.setButtonText (en ? "EN" : "DE");
    languageButton.setTooltip (tr (en, "Sprache: Deutsch / Englisch", "Language: German / English"));
    monoButton.setTooltip (tr (en, "Ausgang zum Abhören mono summieren (einzige Funktion, die das Audio ändert)",
                               "Sum the output to mono for listening (the only function that changes the audio)"));
    holdButton.setTooltip (tr (en, "Anzeige anhalten", "Freeze the display"));
    resetButton.setButtonText (tr (en, "Messung zurücksetzen", "Reset measurement"));
    resetButton.setTooltip (tr (en, "Peak, True Peak, integrierte Lautheit, LRA und History neu beginnen",
                                "Restart peak, true peak, integrated loudness, LRA and history"));
    viewButtons[0].setButtonText (tr (en, "Spektrum", "Spectrum"));
    viewButtons[1].setButtonText (isChannel ? juce::String ("Stereo M/S") : juce::String ("Phase A/B"));
    viewButtons[2].setButtonText (tr (en, "Mono-Verlust", "Mono loss"));
    groupBox.setTooltip (tr (en, "Vergleichsgruppe", "Comparison group"));
    if (isChannel)
    {
        modeBox.setTooltip (tr (en, "Nur Senden schaltet die Analyse ab, um CPU zu sparen", "Send only switches the analysis off to save CPU"));
    }
    // Rebuild the item texts in the current language and keep the selection.
    auto relabel = [] (juce::ComboBox& box, const juce::StringArray& items)
    {
        const int selected = box.getSelectedId();
        box.clear (juce::dontSendNotification);
        box.addItemList (items, 1);
        box.setSelectedId (selected, juce::dontSendNotification);
    };
    relabel (groupBox, { tr (en, "Gruppe 1", "Group 1"), tr (en, "Gruppe 2", "Group 2"), tr (en, "Gruppe 3", "Group 3"), tr (en, "Gruppe 4", "Group 4") });
    if (isChannel)
        relabel (modeBox, { tr (en, "Analyse", "Analyse"), tr (en, "Analyse + Senden", "Analyse + send"), tr (en, "Nur Senden", "Send only") });
}

void Panel::refresh()
{
    if (! holdButton.getToggleState())
    {
        processor.getEngine().copySnapshot (*snapshot);
        if (auto* busLink = processor.getBusLink())
            busLink->getStatus (link);
    }
    if (snapshot->overflow)
        gapShownUntil = juce::Time::getMillisecondCounter() + 2000;
    fullScore = processor.getMeter().fullScore();
    bandScore = processor.getMeter().bandScore();

    auto decay = [] (float& shown, float& hold, float now)
    {
        shown = juce::jmax (now, shown * 0.82f);
        hold = juce::jmax (now, hold * 0.995f);
    };
    decay (meterLeft, peakHoldLeft, snapshot->meterLeft);
    decay (meterRight, peakHoldRight, snapshot->meterRight);
    sendPeak = juce::jmax (processor.takeInputPeak(), sendPeak * 0.8f);

    if (monoButton.getToggleState() != processor.isMonoMonitor())
        monoButton.setToggleState (processor.isMonoMonitor(), juce::dontSendNotification);
    if (lastMono != processor.isMonoMonitor() || lastAnalysing != processor.isAnalysing())
        layoutControls();
}

void Panel::resized()
{
    layoutControls();
}

void Panel::layoutControls()
{
    lastMono = processor.isMonoMonitor();
    lastAnalysing = processor.isAnalysing();

    auto r = getLocalBounds();
    headerArea = r.removeFromTop (64);
    warnArea = lastMono ? r.removeFromTop (26) : juce::Rectangle<int>();
    sourceArea = r.removeFromTop (50);
    footerArea = r.removeFromBottom (30);
    adviceArea = r.removeFromBottom (36);
    historyArea = r.removeFromBottom (112);
    workArea = r;

    auto work = r;
    monitorArea = work.removeFromRight (300);
    auto analysis = work;
    toolbarArea = analysis.removeFromTop (42);
    readoutArea = {};
    auto rest = analysis;
    const int waveTitle = 28, strip = 36, readout = 30;
    const int graphs = rest.getHeight() - waveTitle - strip - readout;
    spectrumArea = rest.removeFromTop ((int) (graphs * 0.56));
    stripArea = rest.removeFromTop (strip);
    readoutArea = rest.removeFromTop (readout);
    waveTitleArea = rest.removeFromTop (waveTitle);
    waveArea = rest;

    // Monitor column
    auto m = monitorArea;
    monitorTitle = m.removeFromTop (32);
    m.removeFromTop (8);
    resetRect = m.removeFromBottom (40).reduced (12, 5);
    valuesRect = m.removeFromBottom (126);
    stereoFoot = m.removeFromBottom (26);
    auto body = m;
    meterRect = body.removeFromLeft (98).reduced (10, 0);
    auto column = body.reduced (8, 0);
    scoreRect = column.removeFromTop (64);
    bandRow = column.removeFromTop (22);
    sliderRect = column.removeFromTop (24);
    correlationRect = column.removeFromTop (42);
    gonioRect = column.withSizeKeepingCentre (juce::jmin (column.getWidth(), column.getHeight()), juce::jmin (column.getWidth(), column.getHeight()));

    // Controls
    monoButton.setBounds (headerArea.getRight() - margin - 76, headerArea.getCentreY() - 15, 76, 30);
    languageButton.setBounds (monoButton.getX() - 58, headerArea.getCentreY() - 15, 48, 30);

    const int sy = sourceArea.getCentreY() - 15;
    if (isChannel)
    {
        modeBox.setBounds (236, sy, 176, 30);
        slotBox.setBounds (420, sy, 96, 30);
        groupBox.setBounds (524, sy, 116, 30);
        slotBox.setEnabled (processor.isSending());
        groupBox.setEnabled (processor.isSending());
    }
    else
    {
        groupBox.setBounds (572, sy, 116, 30);
    }

    for (int i = 0; i < 3; ++i)
        viewButtons[i].setBounds (toolbarArea.getX() + 12 + i * 120, toolbarArea.getCentreY() - 15, 112, 30);
    holdButton.setBounds (toolbarArea.getRight() - 12 - 64, toolbarArea.getCentreY() - 15, 64, 30);
    for (int i = 0; i < 3; ++i)
        historyButtons[i].setBounds (historyArea.getRight() - 12 - (3 - i) * 58, historyArea.getY() + 4, 52, 24);
    sliderRect = sliderRect.reduced (2, 0);
    cutoffSlider.setBounds (sliderRect);
    resetButton.setBounds (resetRect);

    const bool analysing = lastAnalysing;
    for (auto* comp : std::initializer_list<juce::Component*> { &holdButton, &resetButton, &cutoffSlider,
                                                                &viewButtons[0], &viewButtons[1], &viewButtons[2],
                                                                &historyButtons[0], &historyButtons[1], &historyButtons[2] })
        comp->setVisible (analysing);
}

//==============================================================================
void Panel::paint (juce::Graphics& g)
{
    g.fillAll (c::panel);
    paintHeader (g);

    if (lastMono)
    {
        g.setColour (c::warnBack);
        g.fillRect (warnArea);
        g.setColour (c::warnText);
        g.setFont (font (14.0f));
        g.drawText (tr (english(), "MONO-ABHÖREN AKTIV · Ausgang mono, die Analyse misst weiter das Original. Vor dem Export ausschalten.",
                        "MONO MONITOR ON · output is mono, the analysis still measures the original. Switch off before exporting."),
                    warnArea.reduced (margin, 0), juce::Justification::centredLeft);
    }

    paintSourceBar (g, sourceArea);

    if (! lastAnalysing)
    {
        paintSendOnly (g, workArea.getUnion (historyArea).getUnion (adviceArea));
        paintFooter (g, footerArea);
        return;
    }

    // Analysis column
    g.setColour (c::bar);
    g.fillRect (toolbarArea);
    g.setColour (c::sub);
    g.setFont (font (13.5f));
    g.drawText ("FFT 4096", holdButton.getX() - 90, toolbarArea.getY(), 80, toolbarArea.getHeight(), juce::Justification::centredRight);

    paintSpectrum (g, spectrumArea);
    paintBandStrip (g, stripArea);
    paintReadout (g, readoutArea);
    paintWaveform (g, waveTitleArea.getUnion (waveArea));
    g.setColour (c::dark);
    g.drawVerticalLine (monitorArea.getX(), (float) workArea.getY(), (float) workArea.getBottom());
    paintMonitor (g, monitorArea);
    paintHistory (g, historyArea);
    paintAdvice (g, adviceArea);
    paintFooter (g, footerArea);
}

void Panel::paintHeader (juce::Graphics& g)
{
    auto r = headerArea;
    g.setGradientFill (juce::ColourGradient (c::headTop, 0, (float) r.getY(), c::headBottom, 0, (float) r.getBottom(), false));
    g.fillRect (r);
    g.setColour (c::dark);
    g.drawHorizontalLine (r.getBottom() - 1, 0, (float) getWidth());

    g.setFont (font (27.0f));
    g.setColour (c::text);
    g.drawText ("Phase", margin + 4, r.getY(), 90, r.getHeight(), juce::Justification::centredLeft);
    const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "Phase");
    g.setColour (c::a);
    g.drawText ("Lab", margin + 4 + w, r.getY(), 60, r.getHeight(), juce::Justification::centredLeft);
    const int end = margin + 4 + w + juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "Lab") + 16;
    g.setColour (c::line);
    g.fillRect (end, r.getY() + 18, 1, r.getHeight() - 36);
    g.setFont (font (15.0f));
    g.setColour (juce::Colour (0xffbdc8ce));
    g.drawText (isChannel ? "CHANNEL ANALYZER" : "BUS ANALYZER", end + 16, r.getY(), 220, r.getHeight(), juce::Justification::centredLeft);
}

void Panel::paintSourceBar (juce::Graphics& g, juce::Rectangle<int> r)
{
    const bool en = english();
    g.setColour (c::bar);
    g.fillRect (r);
    g.setColour (juce::Colour (0xff4c555c));
    g.drawHorizontalLine (r.getBottom() - 1, 0, (float) getWidth());

    auto tag = [&g] (juce::Rectangle<int> box, const juce::String& text, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawRect (box, 1);
        g.setFont (font (14.0f));
        g.drawText (text, box, juce::Justification::centred);
    };

    if (isChannel)
    {
        tag ({ margin, r.getCentreY() - 13, 30, 26 }, "IN", c::a);
        const auto name = processor.getTrackName();
        g.setColour (c::text);
        g.setFont (font (16.0f));
        g.drawText (name.isEmpty() ? tr (en, "Spur", "Track") : name, margin + 40, r.getY(), 120, r.getHeight(), juce::Justification::centredLeft);
        g.setColour (c::sub);
        g.setFont (font (13.0f));
        g.drawText (processor.getTotalNumInputChannels() > 1 ? "Stereo" : "Mono", margin + 150, r.getY(), 64, r.getHeight(), juce::Justification::centredLeft);

        juce::String status;
        juce::Colour colour = c::sub;
        const auto slot = juce::String (processor.getSlot() == 0 ? "A" : "B");
        switch (processor.getSendState())
        {
            case SendState::off:         status = tr (en, "LOKAL · Messung gültig", "LOCAL · measurement valid"); break;
            case SendState::sending:     status = tr (en, "SENDET AN ", "SENDING TO ") + slot; colour = c::good; break;
            case SendState::busy:        status = "Slot " + slot + tr (en, " belegt", " in use"); colour = c::amber; break;
            case SendState::unavailable: status = tr (en, "Verbinde …", "Connecting …"); break;
        }
        if (processor.isSending() && processor.getSendState() == SendState::off)
            status = tr (en, "Wartet auf Audio …", "Waiting for audio …");
        g.setColour (colour);
        g.setFont (font (14.0f));
        g.fillEllipse ((float) r.getRight() - margin - 230, (float) r.getCentreY() - 3.5f, 7, 7);
        g.drawText (status, r.getRight() - margin - 218, r.getY(), 218, r.getHeight(), juce::Justification::centredLeft);
        return;
    }

    for (int i = 0; i < 2; ++i)
    {
        const auto& s = i == 0 ? link.a : link.b;
        const int x = margin + i * 276;
        tag ({ x, r.getCentreY() - 13, 26, 26 }, i == 0 ? "A" : "B", i == 0 ? c::a : c::b);
        g.setColour (s.connected ? c::text : c::sub);
        g.setFont (font (16.0f));
        const juce::String name = ! s.connected ? juce::String::fromUTF8 ("–")
                                : s.name.empty() ? juce::String ("Channel Analyzer") : juce::String::fromUTF8 (s.name.c_str());
        g.drawText (name, x + 36, r.getY(), 130, r.getHeight(), juce::Justification::centredLeft);
        g.setFont (font (13.0f));
        g.setColour (! s.connected ? c::sub : s.stale ? c::amber : c::good);
        g.drawText (! s.connected ? tr (en, "fehlt", "missing") : s.stale ? tr (en, "liefert nichts", "no data") : tr (en, "verbunden", "connected"),
                    x + 166, r.getY(), 96, r.getHeight(), juce::Justification::centredLeft);
    }

    const bool valid = link.state == LinkState::valid;
    g.setColour (valid ? c::good : (link.state == LinkState::disconnected ? c::sub : c::amber));
    g.fillEllipse ((float) groupBox.getRight() + 18, (float) r.getCentreY() - 3.5f, 7, 7);
    g.setFont (font (14.0f));
    g.drawText (linkStateText (link.state, en), groupBox.getRight() + 30, r.getY(), r.getRight() - groupBox.getRight() - 30 - margin,
                r.getHeight(), juce::Justification::centredLeft);
}

void Panel::paintSpectrum (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool en = english();
    g.setColour (c::graph);
    g.fillRect (area);
    auto r = area.toFloat().withTrimmedLeft (46).withTrimmedRight (18).withTrimmedTop (24).withTrimmedBottom (22);
    drawFrequencyGrid (g, r, true);
    g.setColour (c::sub);
    g.setFont (font (12.5f));
    g.drawText ("Hz", (int) r.getRight() - 30, area.getY() + 4, 30, 16, juce::Justification::centredRight);

    const bool fromLink = linkValid() && link.spectrum.valid;
    const auto& own = snapshot->spectrum;
    auto message = [&] (const juce::String& text)
    {
        g.setColour (c::sub);
        g.setFont (font (15.0f));
        g.drawText (text, r.toNearestInt(), juce::Justification::centred);
    };

    if (view == View::second && ! isChannel)
    {
        // Phase of B relative to A per third octave
        for (int v : { 180, 90, 0, -90, -180 })
        {
            const float y = r.getY() + (180.0f - v) / 360.0f * r.getHeight();
            g.setColour (c::grid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (c::sub);
            g.drawText (juce::String (v) + juce::String::fromUTF8 ("°"), (int) r.getX() - 44, (int) y - 8, 38, 16, juce::Justification::centredRight);
        }
        g.setColour (c::sub);
        g.drawText (tr (en, "PHASE B GEGENÜBER A (Terzbänder)", "PHASE OF B RELATIVE TO A (third octaves)"), (int) r.getX() + 8, area.getY() + 4, 400, 16, juce::Justification::centredLeft);
        if (! fromLink)
            return message (linkValid() ? tr (en, "Fenster öffnen sich …", "Collecting data …") : linkStateText (link.state, en));

        juce::Path p;
        bool started = false;
        for (int b = 0; b < bandCount; ++b)
        {
            const double ph = link.spectrum.phase[(size_t) b];
            if (ph <= noBand + 1.0) { started = false; continue; }
            const float x = (float) xOfFrequency (bandCentre (b), r);
            const float y = r.getY() + (float) ((180.0 - ph) / 360.0) * r.getHeight();
            if (! started) p.startNewSubPath (x, y); else p.lineTo (x, y);
            started = true;
            g.setColour (c::a);
            g.fillEllipse (x - 3, y - 3, 6, 6);
        }
        g.setColour (c::a);
        g.strokePath (p, juce::PathStrokeType (1.6f));
        return;
    }

    if (view == View::monoLoss)
    {
        for (int v : { 0, -6, -12, -18, -24 })
        {
            const float y = r.getY() + (-v / 24.0f) * r.getHeight();
            g.setColour (c::grid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (c::sub);
            g.drawText (number (v, 0, en), (int) r.getX() - 44, (int) y - 8, 38, 16, juce::Justification::centredRight);
        }
        g.setColour (c::sub);
        g.drawText (tr (en, "MONO-VERLUST · Monosumme gegenüber L/R (dB)", "MONO LOSS · mono sum compared with L/R (dB)")
                        + (isChannel ? juce::String() : tr (en, " · Bus-Eingang", " · bus input")),
                    (int) r.getX() + 8, area.getY() + 4, 460, 16, juce::Justification::centredLeft);
        if (! own.valid)
            return message (tr (en, "Kein Signal", "No signal"));

        juce::Path line, fill;
        fill.startNewSubPath (r.getX(), r.getY());
        int worst = -1;
        for (int b = 0; b < bandCount; ++b)
        {
            const double v = own.monoLoss[(size_t) b] <= noBand + 1.0 ? 0.0 : own.monoLoss[(size_t) b];
            const float x0 = (float) xOfFrequency (bandCentre (b) * std::pow (2.0, -1.0 / 6.0), r);
            const float x1 = (float) xOfFrequency (bandCentre (b) * std::pow (2.0, 1.0 / 6.0), r);
            const float y = r.getY() + (float) (juce::jlimit (0.0, 24.0, -v) / 24.0) * r.getHeight();
            if (b == 0) line.startNewSubPath (juce::jmax (r.getX(), x0), y); else line.lineTo (x0, y);
            line.lineTo (juce::jmin (r.getRight(), x1), y);
            fill.lineTo (juce::jmax (r.getX(), x0), y);
            fill.lineTo (juce::jmin (r.getRight(), x1), y);
            if (v < -3.0 && (worst < 0 || v < own.monoLoss[(size_t) worst]))
                worst = b;
        }
        fill.lineTo (r.getRight(), r.getY());
        fill.closeSubPath();
        g.setColour (c::loss.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (c::loss);
        g.strokePath (line, juce::PathStrokeType (1.6f));
        if (worst >= 0)
        {
            const float x = (float) xOfFrequency (bandCentre (worst), r);
            g.setFont (font (13.0f));
            g.drawText (number (own.monoLoss[(size_t) worst], 0, en) + " dB · " + frequency (bandCentre (worst), en), (int) x + 8,
                        (int) (r.getY() + r.getHeight() * 0.62f), 180, 16, juce::Justification::centredLeft);
        }
        return;
    }

    // Spectrum views
    for (int db = 0; db >= -72; db -= 12)
    {
        const float y = r.getY() + (-db / 72.0f) * r.getHeight();
        g.setColour (c::grid);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (c::sub);
        g.setFont (font (12.5f));
        g.drawText (number (db, 0, en), (int) r.getX() - 44, (int) y - 8, 38, 16, juce::Justification::centredRight);
    }
    g.drawText ("dBFS", (int) r.getX() - 44, area.getY() + 4, 40, 16, juce::Justification::centredLeft);

    if (view == View::second)   // channel: mid and side
    {
        g.setColour (c::sub);
        g.drawText ("MID / SIDE", (int) r.getX() + 8, area.getY() + 4, 300, 16, juce::Justification::centredLeft);
        if (! own.valid)
            return message (tr (en, "Kein Signal", "No signal"));
        drawCurve (g, own.mid, r, c::a, -72.0f, true);
        drawCurve (g, own.side, r, c::b, -72.0f, true);
        return;
    }

    if (fromLink)
    {
        g.setColour (c::sub);
        g.drawText ("A " + juce::String::fromUTF8 ("·") + " " + (link.a.name.empty() ? juce::String ("A") : juce::String::fromUTF8 (link.a.name.c_str()))
                        + "     /     B " + juce::String::fromUTF8 ("·") + " " + (link.b.name.empty() ? juce::String ("B") : juce::String::fromUTF8 (link.b.name.c_str())),
                    (int) r.getX() + 8, area.getY() + 4, 500, 16, juce::Justification::centredLeft);
        drawCurve (g, link.spectrum.x, r, c::a, -72.0f, true);
        drawCurve (g, link.spectrum.y, r, c::b, -72.0f, true);
        return;
    }

    g.setColour (c::sub);
    g.drawText (isChannel ? tr (en, "RESONANZSPEKTRUM", "RESONANCE SPECTRUM") : tr (en, "SPEKTRUM · Bus-Eingang", "SPECTRUM · bus input"),
                (int) r.getX() + 8, area.getY() + 4, 300, 16, juce::Justification::centredLeft);
    if (! own.valid)
        return message (tr (en, "Kein Signal", "No signal"));
    drawCurve (g, own.mid, r, c::a, -72.0f, true);
    if (own.resonanceHz > 0.0)
    {
        const float x = (float) xOfFrequency (own.resonanceHz, r);
        g.setColour (c::peak);
        g.drawVerticalLine ((int) x, r.getY() + 6, r.getBottom());
        g.setFont (font (13.0f));
        g.drawText (frequency (own.resonanceHz, en), (int) x + 6, (int) r.getY() + 22, 90, 16, juce::Justification::centredLeft);
    }
}

void Panel::paintBandStrip (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool en = english();
    g.setColour (c::graph);
    g.fillRect (area);
    g.setColour (c::dark);
    g.drawHorizontalLine (area.getY(), (float) area.getX(), (float) area.getRight());

    auto r = area.toFloat().withTrimmedLeft (46).withTrimmedRight (18).withTrimmedTop (5).withTrimmedBottom (15);
    const bool fromLink = linkValid() && link.spectrum.valid;
    const auto& source = fromLink ? link.spectrum : snapshot->spectrum;
    g.setColour (c::sub);
    g.setFont (font (12.0f));
    g.drawText (fromLink ? juce::String::fromUTF8 ("A↔B") : juce::String ("L/R"), (int) r.getX() - 44, (int) r.getY(), 38, (int) r.getHeight(),
                juce::Justification::centredRight);
    g.setColour (c::good);
    g.drawText (tr (en, "gleichphasig", "in phase"), (int) r.getRight() - 90, (int) r.getBottom() + 1, 90, 13, juce::Justification::centredRight);
    g.setColour (c::bad);
    g.drawText (tr (en, "auslöschend", "cancelling"), (int) r.getRight() - 190, (int) r.getBottom() + 1, 96, 13, juce::Justification::centredRight);

    for (int b = 0; b < bandCount; ++b)
    {
        const float x0 = (float) xOfFrequency (bandCentre (b) * std::pow (2.0, -1.0 / 6.0), r);
        const float x1 = (float) xOfFrequency (bandCentre (b) * std::pow (2.0, 1.0 / 6.0), r);
        const auto box = juce::Rectangle<float> (juce::jmax (r.getX(), x0) + 0.5f, r.getY(), juce::jmin (r.getRight(), x1) - juce::jmax (r.getX(), x0) - 1.0f, r.getHeight());
        const double v = source.valid ? source.correlation[(size_t) b] : noBand;
        g.setColour (v <= noBand + 1.0 ? c::grid : correlationColour (v).withAlpha (0.88f));
        g.fillRect (box);
    }
}

void Panel::paintReadout (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool en = english();
    g.setColour (juce::Colour (0xff20292f));
    g.fillRect (area);
    g.setFont (font (14.0f));
    auto r = area.reduced (14, 0);

    if (isChannel)
    {
        const auto& s = snapshot->spectrum;
        g.setColour (c::sub);
        g.drawText (s.valid && s.resonanceHz > 0.0
                        ? tr (en, "Resonanz ", "Resonance ") + frequency (s.resonanceHz, en) + "  +" + number (s.resonanceDb, 1, en) + tr (en, " dB relativ", " dB relative")
                        : tr (en, "Keine schmale Überhöhung", "No narrow peak"),
                    r, juce::Justification::centredLeft);
        g.drawText (tr (en, "L/R-Korrelation ", "L/R correlation ") + (snapshot->stereo.valid ? number (snapshot->stereo.correlation, 2, en, true) : juce::String::fromUTF8 ("–")),
                    r, juce::Justification::centredRight);
        return;
    }

    const bool valid = link.state == LinkState::valid && link.result.enoughSignal;
    const auto& res = link.result;
    const auto dash = juce::String::fromUTF8 ("–");
    g.setColour (c::sub);
    g.drawText (juce::String::fromUTF8 ("A ↔ B ") + (valid ? number (res.correlation, 2, en, true) : dash) + tr (en, " Korrelation", " correlation"),
                r, juce::Justification::centredLeft);
    juce::String offset = tr (en, "Versatz ", "Offset ") + dash;
    if (valid && res.clearOffset && link.sampleRate > 0)
        offset = tr (en, "Versatz ", "Offset ") + (res.offsetSamples > 0 ? "+" : "") + juce::String (res.offsetSamples) + " Samples ("
                 + number (1000.0 * res.offsetSamples / link.sampleRate, 2, en, true) + " ms)";
    g.drawText (offset, r, juce::Justification::centred);
    g.drawText ("A+B < " + frequency (cutoffSlider.getValue(), en) + "  " + (link.state == LinkState::valid && res.sumScore >= 0 ? juce::String (res.sumScore) + "/100" : dash),
                r, juce::Justification::centredRight);
}

void Panel::paintWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool en = english();
    auto title = area.removeFromTop (28);
    g.setColour (c::strip);
    g.fillRect (title);
    g.setColour (juce::Colour (0xff46515a));
    g.drawHorizontalLine (title.getY(), (float) title.getX(), (float) title.getRight());

    const bool fromLink = linkValid() && link.waveLength > 0;
    const float* x = fromLink ? link.waveA.data() : snapshot->waveLeft.data();
    const float* y = fromLink ? link.waveB.data() : snapshot->waveRight.data();
    const int n = fromLink ? link.waveLength : snapshot->waveLength;

    g.setFont (font (14.0f));
    g.setColour (c::text);
    g.drawText ("DUAL-WAVEFORM", title.reduced (13, 0), juce::Justification::centredLeft);
    const auto keyA = fromLink ? juce::String ("A") : juce::String (tr (en, "L · links", "L · left"));
    const auto keyB = fromLink ? juce::String ("B") : juce::String (tr (en, "R · rechts", "R · right"));
    const int kx = title.getCentreX() - 80;
    g.setColour (c::a); g.fillRect (kx, title.getCentreY() - 1, 18, 2);
    g.setColour (c::sub); g.drawText (keyA, kx + 24, title.getY(), 70, title.getHeight(), juce::Justification::centredLeft);
    g.setColour (c::b); g.fillRect (kx + 100, title.getCentreY() - 1, 18, 2);
    g.setColour (c::sub); g.drawText (keyB, kx + 124, title.getY(), 80, title.getHeight(), juce::Justification::centredLeft);
    g.drawText ("40 ms", title.reduced (13, 0), juce::Justification::centredRight);

    g.setColour (c::graph);
    g.fillRect (area);
    auto r = area.toFloat().withTrimmedLeft (46).withTrimmedRight (18).withTrimmedTop (10).withTrimmedBottom (22);
    for (int v : { 1, 0, -1 })
    {
        const float yy = r.getCentreY() - v * r.getHeight() * 0.5f;
        g.setColour (c::grid);
        g.drawHorizontalLine ((int) yy, r.getX(), r.getRight());
        g.setColour (c::sub);
        g.setFont (font (12.5f));
        g.drawText (v > 0 ? "+1" : v < 0 ? juce::String::fromUTF8 ("−1") : juce::String ("0"), (int) r.getX() - 44, (int) yy - 8, 38, 16, juce::Justification::centredRight);
    }
    for (int j = 0; j <= 4; ++j)
    {
        const float xx = r.getX() + r.getWidth() * j / 4.0f;
        g.setColour (c::grid);
        g.drawVerticalLine ((int) xx, r.getY(), r.getBottom());
        g.setColour (c::sub);
        const int bx = j == 0 ? (int) xx : j == 4 ? (int) xx - 60 : (int) xx - 30;
        g.drawText (juce::String (j * 10) + (j == 4 ? " ms" : ""), bx, (int) r.getBottom() + 4, 60, 16,
                    j == 0 ? juce::Justification::centredLeft : j == 4 ? juce::Justification::centredRight : juce::Justification::centred);
    }
    if (n <= 1)
    {
        g.setColour (c::sub);
        g.setFont (font (15.0f));
        g.drawText (tr (en, "Kein Signal", "No signal"), r.toNearestInt(), juce::Justification::centred);
        return;
    }

    float top = 0.05f;
    for (int i = 0; i < n; ++i)
        top = juce::jmax (top, std::abs (x[i]), std::abs (y[i]));
    for (int s = 0; s < 2; ++s)
    {
        const float* data = s == 0 ? x : y;
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float px = r.getX() + r.getWidth() * i / (float) (n - 1);
            const float py = r.getCentreY() - data[i] / top * r.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (s == 0 ? c::a : c::b);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }
}

void Panel::paintMonitor (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool en = english();
    g.setColour (c::monitor);
    g.fillRect (area);
    g.setColour (c::strip);
    g.fillRect (monitorTitle);
    g.setColour (c::text);
    g.setFont (font (15.0f));
    g.drawText (isChannel ? tr (en, "SPUREINGANG · STEREO", "TRACK INPUT · STEREO") : tr (en, "BUS-EINGANG · STEREO", "BUS INPUT · STEREO"),
                monitorTitle.reduced (13, 0), juce::Justification::centredLeft);

    paintMeters (g, meterRect);

    // Mono score, band score
    auto s = scoreRect;
    g.setColour (juce::Colour (0xffbecad1));
    g.setFont (font (14.0f));
    g.drawText ("MONO-SCORE", s.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (c::text);
    g.setFont (font (42.0f));
    const auto score = fullScore < 0 ? juce::String::fromUTF8 ("–") : juce::String (fullScore);
    g.drawText (score, s, juce::Justification::centredLeft);
    if (fullScore >= 0)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), score);
        g.setColour (c::sub);
        g.setFont (font (15.0f));
        g.drawText ("/100", s.withTrimmedLeft (w + 4), juce::Justification::centredLeft);
    }
    g.setColour (c::sub);
    g.setFont (font (13.5f));
    g.drawText (tr (en, "unter ", "below ") + frequency (cutoffSlider.getValue(), en), bandRow, juce::Justification::centredLeft);
    g.setColour (c::text);
    g.setFont (font (16.0f));
    g.drawText ((bandScore < 0 ? juce::String::fromUTF8 ("–") : juce::String (bandScore)) + " /100", bandRow, juce::Justification::centredRight);

    g.setColour (c::sub);
    g.setFont (font (13.5f));
    auto cr = correlationRect;
    g.drawText (tr (en, "L/R-Korrelation", "L/R correlation"), cr.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (c::text);
    g.setFont (font (21.0f));
    g.drawText (snapshot->stereo.valid ? number (snapshot->stereo.correlation, 2, en, true) : juce::String::fromUTF8 ("–"), cr, juce::Justification::centredLeft);

    paintGoniometer (g, gonioRect);

    // Side share and balance
    g.setColour (juce::Colour (0xffb8c6cf));
    g.setFont (font (14.0f));
    const auto& st = snapshot->stereo;
    const auto dash = juce::String::fromUTF8 ("–");
    g.drawText ("Side " + (st.valid ? number (st.sidePercent, 0, en) + " %" : dash), stereoFoot.reduced (12, 0), juce::Justification::centredLeft);
    juce::String balance = dash;
    if (st.valid)
        balance = std::abs (st.balanceDb) < 0.5 ? tr (en, "Mitte", "centre")
                : number (std::abs (st.balanceDb), 1, en) + " dB " + (st.balanceDb > 0 ? tr (en, "rechts", "right") : tr (en, "links", "left"));
    g.drawText ("Balance " + balance, stereoFoot.reduced (12, 0), juce::Justification::centredRight);

    // Levels
    const auto& lv = snapshot->levels;
    struct Cell { juce::String label; juce::String value; juce::String unit; };
    const Cell cells[] = {
        { "Peak", levelText (lv.peak, en), "dBFS" },
        { "True Peak", levelText (lv.truePeak, en), "dBTP" },
        { "RMS", levelText (lv.rms, en), "dBFS" },
        { tr (en, "LUFS · kurz", "LUFS · short"), levelText (lv.shortTerm, en), "" },
        { tr (en, "LUFS · integr.", "LUFS · integr."), levelText (lv.integrated, en), "" },
        { "LRA", levelText (lv.range, en), "LU" },
    };
    g.setColour (c::field);
    g.fillRect (valuesRect);
    for (int i = 0; i < 6; ++i)
    {
        auto cell = juce::Rectangle<int> (valuesRect.getX() + (i % 2) * valuesRect.getWidth() / 2, valuesRect.getY() + (i / 2) * 42,
                                          valuesRect.getWidth() / 2, 42);
        g.setColour (juce::Colour (0xff3a454d));
        g.drawRect (cell.withTrimmedTop (41));
        if (i % 2 == 0)
            g.drawVerticalLine (cell.getRight(), (float) cell.getY(), (float) cell.getBottom());
        auto in = cell.reduced (12, 3);
        g.setColour (c::sub);
        g.setFont (font (13.0f));
        g.drawText (cells[i].label, in.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (i == 1 && lv.truePeak > -1.0 ? c::amber : c::text);
        g.setFont (font (19.0f));
        g.drawText (cells[i].value, in, juce::Justification::centredLeft);
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), cells[i].value);
        g.setColour (c::sub);
        g.setFont (font (12.5f));
        g.drawText (cells[i].unit, in.withTrimmedLeft (w + 5), juce::Justification::centredLeft);
    }
}

void Panel::paintMeters (juce::Graphics& g, juce::Rectangle<int> r)
{
    const int bar = 17, gap = 9;
    const int x0 = r.getX() + 4;
    const int top = r.getY() + 20, bottom = r.getBottom() - 18, height = bottom - top;
    g.setFont (font (12.5f));
    g.setColour (c::sub);
    g.drawText ("L", x0, r.getY(), bar, 16, juce::Justification::centred);
    g.drawText ("R", x0 + bar + gap, r.getY(), bar, 16, juce::Justification::centred);

    const float levels[] = { meterLeft, meterRight }, holds[] = { peakHoldLeft, peakHoldRight };
    for (int s = 0; s < 2; ++s)
    {
        const int x = x0 + s * (bar + gap);
        const float db = juce::Decibels::gainToDecibels (levels[s], -60.0f);
        for (int j = 0; j < 48; ++j)
        {
            const float segDb = -j * 1.25f;
            const bool lit = segDb <= db;
            const int y = top + j * height / 48;
            g.setColour (lit ? (segDb > -6 ? juce::Colour (0xffc9b487) : segDb > -18 ? juce::Colour (0xff8ab9ad) : juce::Colour (0xff648e8b))
                             : juce::Colour (0xff334147));
            g.fillRect (x, y, bar, juce::jmax (1, height / 48 - 1));
        }
        const float holdDb = juce::Decibels::gainToDecibels (holds[s], -60.0f);
        if (holdDb > -60.0f)
        {
            const int y = top + (int) (-holdDb / 60.0f * height);
            g.setColour (c::peak);
            g.fillRect (x - 1, y, bar + 2, 2);
        }
    }
    g.setColour (c::sub);
    for (int v : { 0, -6, -12, -24, -36, -48, -60 })
        g.drawText (number (v, 0, english()), x0 + 2 * bar + gap + 4, top + (int) (-v / 60.0f * height) - 7, 30, 14, juce::Justification::centredLeft);
    g.drawText ("dBFS", r.getX(), bottom + 2, r.getWidth(), 16, juce::Justification::centred);
}

void Panel::paintGoniometer (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    g.setColour (c::graph);
    g.fillRect (r);
    const float cx = r.getCentreX(), cy = r.getCentreY(), rr = r.getWidth() * 0.43f;
    g.setColour (c::grid);
    g.drawEllipse (cx - rr, cy - rr, 2 * rr, 2 * rr, 1.0f);
    g.drawEllipse (cx - rr / 2, cy - rr / 2, rr, rr, 1.0f);
    g.drawLine (cx - rr, cy, cx + rr, cy);
    g.drawLine (cx, cy - rr, cx, cy + rr);
    g.drawLine (cx - rr * 0.7f, cy - rr * 0.7f, cx + rr * 0.7f, cy + rr * 0.7f);
    g.drawLine (cx + rr * 0.7f, cy - rr * 0.7f, cx - rr * 0.7f, cy + rr * 0.7f);
    g.setColour (c::sub);
    g.setFont (font (12.0f));
    g.drawText ("L", (int) r.getX() + 4, (int) r.getY() + 2, 12, 12, juce::Justification::centredLeft);
    g.drawText ("R", (int) r.getRight() - 16, (int) r.getY() + 2, 12, 12, juce::Justification::centredRight);

    if (! snapshot->display)
        return;
    float top = 0.02f;
    for (int i = 0; i < StereoMeter::scopePoints; ++i)
        top = juce::jmax (top, std::abs (snapshot->scopeMid[(size_t) i]) + std::abs (snapshot->scopeSide[(size_t) i]));
    g.setColour (c::a.withAlpha (0.45f));
    for (int i = 0; i < StereoMeter::scopePoints; ++i)
    {
        // Mid up, side sideways (left signal to the upper left).
        const float px = cx - snapshot->scopeSide[(size_t) i] / top * rr;
        const float py = cy - snapshot->scopeMid[(size_t) i] / top * rr;
        g.fillRect (px, py, 1.5f, 1.5f);
    }
}

void Panel::paintHistory (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool en = english();
    g.setColour (c::graph);
    g.fillRect (area);
    auto title = area.removeFromTop (32);
    g.setColour (c::strip);
    g.fillRect (title);
    g.setColour (c::text);
    g.setFont (font (14.0f));
    g.drawText ("HISTORY", title.reduced (13, 0), juce::Justification::centredLeft);
    g.setColour (c::a); g.fillRect (title.getX() + 92, title.getCentreY() - 1, 16, 2);
    g.setColour (c::sub);
    g.setFont (font (13.0f));
    g.drawText (tr (en, "L/R-Korrelation", "L/R correlation"), title.getX() + 112, title.getY(), 130, title.getHeight(), juce::Justification::centredLeft);
    g.setColour (c::b); g.fillRect (title.getX() + 246, title.getCentreY() - 1, 16, 2);
    g.setColour (c::sub);
    g.drawText (tr (en, "Mono-Score", "Mono score"), title.getX() + 266, title.getY(), 100, title.getHeight(), juce::Justification::centredLeft);
    if (juce::Time::getMillisecondCounter() < gapShownUntil)
    {
        g.setColour (c::amber);
        g.drawText (tr (en, "Lücke: Analyse kam nicht hinterher", "Gap: the analysis could not keep up"), title.getX() + 380, title.getY(), 300, title.getHeight(), juce::Justification::centredLeft);
    }

    auto r = area.toFloat().withTrimmedLeft (46).withTrimmedRight (18).withTrimmedTop (8).withTrimmedBottom (22);
    for (int v : { 1, 0, -1 })
    {
        const float y = r.getCentreY() - v * r.getHeight() * 0.5f;
        g.setColour (c::grid);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (c::sub);
        g.setFont (font (12.0f));
        g.drawText (v > 0 ? "+1" : v < 0 ? juce::String::fromUTF8 ("−1") : juce::String ("0"), (int) r.getX() - 44, (int) y - 7, 38, 14, juce::Justification::centredRight);
    }

    const int count = historySeconds * 10;
    const History::Point* points = snapshot->history.data() + (History::points - count);
    for (int j = 0; j <= 4; ++j)
    {
        const float x = r.getX() + r.getWidth() * j / 4.0f;
        g.setColour (c::grid);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        const int index = juce::jlimit (0, count - 1, (int) std::lround ((count - 1) * j / 4.0));
        const int ago = juce::roundToInt (historySeconds * (1.0 - j / 4.0));
        juce::String label = j == 4 ? tr (en, "Jetzt", "Now") : juce::String::fromUTF8 ("−") + juce::String (ago) + " s";
        if (points[index].valid && points[index].bar >= 1.0)
            label << tr (en, " · T. ", " · bar ") << juce::String ((int) std::floor (points[index].bar));
        g.setColour (c::sub);
        const int bx = j == 0 ? (int) x : j == 4 ? (int) x - 140 : (int) x - 70;
        g.drawText (label, bx, (int) r.getBottom() + 4, 140, 16,
                    j == 0 ? juce::Justification::centredLeft : j == 4 ? juce::Justification::centredRight : juce::Justification::centred);
    }

    for (int line = 0; line < 2; ++line)
    {
        juce::Path p;
        bool started = false;
        for (int i = 0; i < count; ++i)
        {
            const auto& pt = points[i];
            const float value = line == 0 ? pt.value : (pt.second < 0 ? -2.0f : pt.second / 50.0f - 1.0f);
            if (! pt.valid || value < -1.5f) { started = false; continue; }
            const float x = r.getX() + r.getWidth() * i / (float) (count - 1);
            const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, value) * r.getHeight() * 0.5f;
            if (! started) p.startNewSubPath (x, y); else p.lineTo (x, y);
            started = true;
        }
        g.setColour (line == 0 ? c::a : c::b.withAlpha (0.7f));
        g.strokePath (p, juce::PathStrokeType (line == 0 ? 1.6f : 1.2f));
    }
}

void Panel::paintAdvice (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool en = english();
    const OwnInput in { *snapshot, fullScore, bandScore, cutoffSlider.getValue() };
    const auto advice = isChannel ? adviseOwnInput (in, en) : adviseBus (in, link, en);
    g.setColour (c::adviceBack);
    g.fillRect (area);
    g.setColour (juce::Colour (0xff5d594c));
    g.drawHorizontalLine (area.getY(), (float) area.getX(), (float) area.getRight());
    auto r = area.reduced (14, 0);
    g.setFont (font (14.5f));
    g.setColour (advice.warning ? c::adviceTitle : c::good);
    const int w = juce::jmin (420, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), advice.title) + 4);
    g.drawText (advice.title, r.removeFromLeft (w), juce::Justification::centredLeft);
    g.setColour (c::adviceText);
    g.setFont (font (14.0f));
    g.drawFittedText (advice.text, r.withTrimmedLeft (14), juce::Justification::centredLeft, 1, 0.85f);
}

void Panel::paintFooter (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool en = english();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff343d43), 0, (float) area.getY(), juce::Colour (0xff2c343a), 0, (float) area.getBottom(), false));
    g.fillRect (area);
    g.setColour (juce::Colour (0xff4c565e));
    g.drawHorizontalLine (area.getY(), 0, (float) getWidth());
    auto r = area.reduced (margin, 0);
    g.setFont (font (13.5f));
    g.setColour (c::sub);
    const double rate = processor.getSampleRate();
    g.drawText (rate > 0 ? number (rate / 1000.0, 1, en) + " kHz " + juce::String::fromUTF8 ("·") + " " + juce::String (processor.getBlockSize()) + " Samples"
                         : tr (en, "nicht aktiv", "not active"),
                r, juce::Justification::centredLeft);

    juce::String running;
    if (lastMono)
        running = tr (en, "MONO-ABHÖREN · Ausgang mono", "MONO MONITOR · output mono");
    else if (! lastAnalysing)
        running = tr (en, "ANALYSE AUS · Audio unverändert", "ANALYSIS OFF · audio unchanged");
    else if (holdButton.getToggleState())
        running = tr (en, "ANSICHT GEHALTEN · Audio unverändert", "VIEW HELD · audio unchanged");
    else
        running = tr (en, "ANALYSE AKTIV · Audio unverändert", "ANALYSIS ON · audio unchanged");
    g.setColour (lastMono ? c::amber : c::sub);
    g.drawText (running, r, juce::Justification::centred);
    g.setColour (c::sub);
    g.drawText ("PhaseLab " PHASELAB_VERSION_STRING, r, juce::Justification::centredRight);
}

void Panel::paintSendOnly (juce::Graphics& g, juce::Rectangle<int> area)
{
    const bool en = english();
    g.setColour (c::field);
    g.fillRect (area);
    auto box = area.reduced (40, 40);
    const bool slotB = processor.getSlot() == 1;
    auto letter = juce::Rectangle<int> (box.getX(), box.getY(), 110, 110);
    g.setColour (slotB ? c::b : c::a);
    g.drawRect (letter, 2);
    g.setFont (font (60.0f));
    g.drawText (slotB ? "B" : "A", letter, juce::Justification::centred);

    const auto name = processor.getTrackName();
    g.setColour (c::text);
    g.setFont (font (28.0f));
    g.drawText ((name.isEmpty() ? tr (en, "Nur Senden", "Send only") : name) + tr (en, " · Gruppe ", " · group ") + juce::String (processor.getGroup() + 1)
                    + " " + juce::String::fromUTF8 ("·") + " Slot " + (slotB ? "B" : "A"),
                letter.getRight() + 28, letter.getY() + 8, box.getRight() - letter.getRight() - 28, 36, juce::Justification::centredLeft);
    g.setColour (c::sub);
    g.setFont (font (16.0f));
    g.drawFittedText (tr (en, "Analyse und Anzeigen sind aus, um CPU zu sparen. Das Audio läuft unverändert durch und geht an den Bus Analyzer derselben Gruppe – die Messungen dieser Spur siehst du dort.",
                          "Analysis and displays are off to save CPU. The audio passes through unchanged and goes to the Bus Analyzer of the same group – that is where you see this track's measurements."),
                      letter.getRight() + 28, letter.getY() + 52, box.getRight() - letter.getRight() - 28, 60, juce::Justification::topLeft, 3, 1.0f);

    auto meter = juce::Rectangle<float> ((float) box.getX(), (float) letter.getBottom() + 40, 480.0f, 10.0f);
    g.setColour (juce::Colour (0xff334147));
    g.fillRect (meter);
    const float level = juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (sendPeak, -60.0f) / 60.0f);
    g.setColour (c::good);
    g.fillRect (meter.withWidth (meter.getWidth() * level));
    g.setColour (c::sub);
    g.setFont (font (14.0f));
    g.drawText (sendPeak > 0.001f ? tr (en, "Signal vorhanden", "Signal present") : tr (en, "Kein Signal", "No signal"),
                (int) meter.getX(), (int) meter.getBottom() + 6, 300, 18, juce::Justification::centredLeft);
}

} // namespace phaselab::ui
