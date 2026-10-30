#include "ui/Panel.h"

#include <algorithm>
#include <cmath>
#include <vector>

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
        return v <= noLevel + 1.0 ? juce::String::fromUTF8 ("–") : number (v, places, en, v > 0.0);
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

    languageButton.onClick = [this] { processor.setEnglish (! english()); updateTexts(); shownKind.clear(); updateAdvice(); repaint(); };
    addAndMakeVisible (languageButton);

    holdButton.setClickingTogglesState (true);
    addAndMakeVisible (holdButton);

    resetButton.onClick = [this]
    {
        processor.getEngine().requestReset();
        peakHoldLeft = peakHoldRight = 0.0f;
        clipLeftUntil = clipRightUntil = 0;
    };
    addAndMakeVisible (resetButton);

    for (int i = 0; i < 3; ++i)
    {
        viewButtons[i].setRadioGroupId (1);
        viewButtons[i].setClickingTogglesState (true);
        viewButtons[i].onClick = [this, i] { view = (View) i; repaint(); };
        addAndMakeVisible (viewButtons[i]);

        const int secs = 30 * (i + 1);
        historyButtons[i].setButtonText (juce::String (secs) + " s");
    }
    for (int i = 0; i < 3; ++i)
    {
        historyModeButtons[i].setRadioGroupId (3);
        historyModeButtons[i].setClickingTogglesState (true);
        historyModeButtons[i].onClick = [this, i]
        {
            if (historyModeButtons[i].getToggleState())
                processor.getState().state.setProperty ("historyMode", i, nullptr);
            repaint();
        };
        addAndMakeVisible (historyModeButtons[i]);
    }
    historyModeButtons[juce::jlimit (0, 2, historyMode())].setToggleState (true, juce::dontSendNotification);
    viewButtons[0].setToggleState (true, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i)
    {
        const int secs = i < 3 ? 30 * (i + 1) : 0;
        historyButtons[i].setRadioGroupId (2);
        historyButtons[i].setClickingTogglesState (true);
        historyButtons[i].onClick = [this, secs] { historySeconds = secs; repaint(); };
        addAndMakeVisible (historyButtons[i]);
    }
    historyButtons[3].setButtonText ("Song");
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

    // Waveform zoom: 2 … 400 ms, logarithmic travel (40 ms in the middle-ish)
    waveZoom.setRange (waveformMinSeconds * 1000.0, waveformMaxSeconds * 1000.0, 0.1);
    waveZoom.setSkewFactorFromMidPoint (28.0);
    waveZoom.setValue (processor.getWaveformSeconds() * 1000.0, juce::dontSendNotification);
    waveZoom.setDoubleClickReturnValue (true, 40.0);
    waveZoom.onValueChange = [this] { processor.setWaveformSeconds (waveZoom.getValue() / 1000.0); repaint(); };
    addAndMakeVisible (waveZoom);

    for (int i = 0; i < 2; ++i)
    {
        traceButtons[i].setClickingTogglesState (true);
        traceButtons[i].setToggleState (true, juce::dontSendNotification);
        traceButtons[i].setButtonText (i == 0 ? "A" : "B");
        traceButtons[i].setColour (juce::TextButton::textColourOnId, i == 0 ? c::a : c::b);
        traceButtons[i].setColour (juce::TextButton::textColourOffId, c::sub.withAlpha (0.45f));
        traceButtons[i].onClick = [this] { repaint(); };
        addChildComponent (traceButtons[i]);
    }
    triggerButton.setClickingTogglesState (true);
    triggerButton.setToggleState (processor.getWaveformTrigger(), juce::dontSendNotification);
    triggerButton.onClick = [this] { processor.setWaveformTrigger (triggerButton.getToggleState()); repaint(); };
    addAndMakeVisible (triggerButton);
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

void Panel::showView (int index)
{
    index = juce::jlimit (0, 2, index);
    viewButtons[index].setToggleState (true, juce::dontSendNotification);
    view = (View) index;
    repaint();
}

void Panel::showHistory (int seconds)
{
    const int index = seconds == 0 ? 3 : juce::jlimit (0, 2, seconds / 30 - 1);
    historyButtons[index].setToggleState (true, juce::dontSendNotification);
    historySeconds = seconds;
    songVersion = -1;
    refresh();
    repaint();
}

int Panel::historyMode() const
{
    return juce::jlimit (0, 2, (int) processor.getState().state.getProperty ("historyMode", 2));   // loudness by default
}

void Panel::updateTexts()
{
    const bool en = english();
    languageButton.setButtonText (en ? "EN" : "DE");
    languageButton.setTooltip (tr (en, "Sprache: Deutsch / Englisch", "Language: German / English"));
    monoButton.setTooltip (tr (en, "Ausgang zum Abhören mono summieren (einzige Funktion, die das Audio ändert)",
                               "Sum the output to mono for listening (the only function that changes the audio)"));
    holdButton.setTooltip (tr (en, "Anzeige anhalten", "Freeze the display"));
    waveZoom.setTooltip (tr (en, "Zeitfenster der Waveform: 2 ms bis 400 ms (Doppelklick: 40 ms)",
                             "Waveform time window: 2 ms to 400 ms (double-click: 40 ms)"));
    triggerButton.setButtonText ("Trigger");
    triggerButton.setTooltip (tr (en, "Fenster startet an einem aufsteigenden Nulldurchgang des Basses von A/L – periodische Signale stehen still",
                                  "Window starts at a rising zero crossing of the low end of A/L – periodic signals stand still"));
    for (auto& b : traceButtons)
        b.setTooltip (tr (en, "Kurve ein- oder ausblenden (Waveform und A/B-Spektrum)", "Show or hide this curve (waveform and A/B spectrum)"));
    resetButton.setButtonText (tr (en, "Messung zurücksetzen", "Reset measurement"));
    resetButton.setTooltip (tr (en, "Peak, True Peak, integrierte Lautheit, LRA und History neu beginnen",
                                "Restart peak, true peak, integrated loudness, LRA and history"));
    viewButtons[0].setButtonText (tr (en, "Spektrum", "Spectrum"));
    viewButtons[1].setButtonText (isChannel ? juce::String ("Stereo M/S") : juce::String ("Phase A/B"));
    viewButtons[2].setButtonText (tr (en, "Mono-Verlust", "Mono loss"));
    historyModeButtons[0].setButtonText (tr (en, "Korrelation", "Correlation"));
    historyModeButtons[1].setButtonText (tr (en, "Pegel", "Level"));
    historyModeButtons[2].setButtonText (tr (en, "Lautheit", "Loudness"));
    historyButtons[3].setTooltip (tr (en, "Ganzer Verlauf seit dem letzten Zurücksetzen (bis 2 Stunden)", "Whole history since the last reset (up to two hours)"));
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
    const auto now = juce::Time::getMillisecondCounter();
    if (snapshot->overflow)
        gapShownUntil = now + 2000;
    if (snapshot->clipLeft)
        clipLeftUntil = now + 3000;
    if (snapshot->clipRight)
        clipRightUntil = now + 3000;
    if (historySeconds == 0 && processor.getEngine().getLongHistoryVersion() != songVersion && ! holdButton.getToggleState())
    {
        songVersion = processor.getEngine().getLongHistoryVersion();
        processor.getEngine().copyLongHistory (songPoints);
    }
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
    // A/B switches only in the Bus Analyzer while A and B are compared; hidden curves come back
    // when the switches disappear.
    const bool abSource = ! isChannel && link.state == LinkState::valid && lastAnalysing;
    if (traceButtons[0].isVisible() != abSource)
    {
        for (auto& b : traceButtons)
            b.setVisible (abSource);
        triggerButton.setTopLeftPosition (waveTitleArea.getX() + (abSource ? 260 : 160), triggerButton.getY());
    }
    if (std::abs (waveZoom.getValue() - processor.getWaveformSeconds() * 1000.0) > 0.05 && ! waveZoom.isMouseButtonDown())
        waveZoom.setValue (processor.getWaveformSeconds() * 1000.0, juce::dontSendNotification);   // e.g. after loading a project
    if (lastMono != processor.isMonoMonitor() || lastAnalysing != processor.isAnalysing())
        layoutControls();
    updateAdvice();
}

void Panel::updateAdvice()
{
    // Measurements move with the music; a hint must stay long enough to be read:
    // - the same kind of hint (title without its numbers) refreshes its values at most once a second,
    // - another kind takes over only after it has held for 1.5 s, and after the current one was
    //   visible for 5 s – a warning replaces a calm message without the 5 s wait.
    const bool en = english();
    const OwnInput in { *snapshot, fullScore, bandScore, cutoffSlider.getValue() };
    const auto candidate = isChannel ? adviseOwnInput (in, en) : adviseBus (in, link, en);

    juce::String kind;
    for (auto ch : candidate.title)
        if (! juce::CharacterFunctions::isDigit (ch) && ch != ',' && ch != '.' && ch != '+' && ch != (juce::juce_wchar) 0x2212)
            kind << juce::String::charToString (ch);

    const auto now = juce::Time::getMillisecondCounter();
    if (shownKind.isEmpty() || (kind == shownKind && now - textUpdated >= 1000) || (shownIdle && kind != shownKind))
    {
        if (kind != shownKind)
            shownSince = now;
        shownIdle = candidate.status;
        shownAdvice = candidate;
        shownKind = kind;
        textUpdated = now;
        pendingKind.clear();
        return;
    }
    if (kind == shownKind)
    {
        pendingKind.clear();
        return;
    }
    if (kind != pendingKind)
    {
        pendingKind = kind;
        pendingSince = now;
        return;
    }
    const bool upgrade = candidate.warning && ! shownAdvice.warning;
    if (now - pendingSince >= 1500 && (upgrade || now - shownSince >= 5000))
    {
        shownAdvice = candidate;
        shownKind = kind;
        shownSince = textUpdated = now;
        shownIdle = candidate.status;
        pendingKind.clear();
    }
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
    historyArea = r.removeFromBottom (140);   // room for a loudness timeline
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
    clipRect = resetRect.removeFromRight (resetRect.getWidth() / 2 - 4);
    resetRect.removeFromRight (8);
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
        historyButtons[i].setBounds (historyArea.getRight() - 12 - (4 - i) * 58, historyArea.getY() + 4, 52, 24);
    historyButtons[3].setBounds (historyArea.getRight() - 12 - 58, historyArea.getY() + 4, 52, 24);
    for (int i = 0; i < 3; ++i)   // loudness, level, correlation from left to right
        historyModeButtons[i].setBounds (historyArea.getX() + 96 + (2 - i) * 98, historyArea.getY() + 4, 92, 24);
    sliderRect = sliderRect.reduced (2, 0);
    cutoffSlider.setBounds (sliderRect);
    resetButton.setBounds (resetRect);
    waveZoom.setBounds (waveTitleArea.getRight() - 12 - 150, waveTitleArea.getCentreY() - 9, 150, 18);
    for (int i = 0; i < 2; ++i)
        traceButtons[i].setBounds (waveTitleArea.getX() + 160 + i * 46, waveTitleArea.getCentreY() - 11, 40, 22);
    triggerButton.setBounds (waveTitleArea.getX() + (traceButtons[0].isVisible() ? 260 : 160), waveTitleArea.getCentreY() - 11, 72, 22);

    const bool analysing = lastAnalysing;
    for (auto* comp : std::initializer_list<juce::Component*> { &holdButton, &resetButton, &cutoffSlider, &waveZoom, &triggerButton,
                                                                &viewButtons[0], &viewButtons[1], &viewButtons[2],
                                                                &historyButtons[0], &historyButtons[1], &historyButtons[2], &historyButtons[3],
                                                                &historyModeButtons[0], &historyModeButtons[1], &historyModeButtons[2] })
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

        g.setFont (font (12.0f));
        g.setColour (c::good.withAlpha (0.8f));
        g.drawText (tr (en, "gleichphasig", "in phase"), (int) r.getRight() - 110, (int) (r.getCentreY() - 16), 104, 14, juce::Justification::centredRight);
        g.setColour (c::bad.withAlpha (0.8f));
        g.drawText (tr (en, "gegenphasig", "opposite"), (int) r.getRight() - 110, (int) r.getY() + 2, 104, 14, juce::Justification::centredRight);

        // Phase only means something where A and B have something in common: bands with a band
        // correlation of at least 0.3 are drawn and joined, the others only as faint dots.
        juce::Path p;
        bool started = false;
        double previous = 0.0;
        for (int b = 0; b < bandCount; ++b)
        {
            const double ph = link.spectrum.phase[(size_t) b];
            const double coherence = std::abs (link.spectrum.correlation[(size_t) b]);
            if (ph <= noBand + 1.0) { started = false; continue; }
            const float x = (float) xOfFrequency (bandCentre (b), r);
            const float y = r.getY() + (float) ((180.0 - ph) / 360.0) * r.getHeight();
            const bool clear = coherence >= 0.3;
            if (clear && started && std::abs (ph - previous) < 180.0)
                p.lineTo (x, y);
            else if (clear)
                p.startNewSubPath (x, y);
            started = clear;
            previous = ph;
            g.setColour (c::a.withAlpha (clear ? 1.0f : 0.25f));
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
        if (traceButtons[0].getToggleState())
            drawCurve (g, link.spectrum.x, r, c::a, -72.0f, true);
        if (traceButtons[1].getToggleState())
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
        offset = tr (en, "Versatz ", "Offset ") + (res.offsetSamples > 0 ? "+" : "") + juce::String (res.offsetSamples) + tr (en, " Samples (", " samples (")
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

    const bool fromLink = linkValid() && link.wave.bins > 0;
    const auto& w = fromLink ? link.wave : snapshot->wave;
    const double windowMs = w.bins > 0 ? w.seconds * 1000.0 : waveZoom.getValue();

    auto msText = [en] (double ms)
    {
        return ms < 10.0 ? number (ms, 1, en) + " ms" : juce::String (juce::roundToInt (ms)) + " ms";
    };

    g.setFont (font (14.0f));
    g.setColour (c::text);
    g.drawText ("DUAL-WAVEFORM", title.reduced (13, 0), juce::Justification::centredLeft);
    if (! traceButtons[0].isVisible())
    {
        // Without the A/B switches: a plain colour key for the two curves.
        const auto keyA = fromLink ? juce::String ("A") : tr (en, "L · links", "L · left");
        const auto keyB = fromLink ? juce::String ("B") : tr (en, "R · rechts", "R · right");
        const int kx = triggerButton.getRight() + 22;
        g.setFont (font (13.5f));
        g.setColour (c::a); g.fillRect (kx, title.getCentreY() - 1, 18, 2);
        g.setColour (c::sub); g.drawText (keyA, kx + 24, title.getY(), 80, title.getHeight(), juce::Justification::centredLeft);
        g.setColour (c::b); g.fillRect (kx + 100, title.getCentreY() - 1, 18, 2);
        g.setColour (c::sub); g.drawText (keyB, kx + 124, title.getY(), 90, title.getHeight(), juce::Justification::centredLeft);
    }
    g.setColour (c::sub);
    g.setFont (font (14.0f));
    g.drawText (tr (en, "Zeitfenster ", "Window ") + msText (waveZoom.getValue()), waveZoom.getX() - 150, title.getY(), 142, title.getHeight(),
                juce::Justification::centredRight);

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
        const int bx = j == 0 ? (int) xx : j == 4 ? (int) xx - 70 : (int) xx - 35;
        const double ms = windowMs * j / 4.0;
        g.drawText (j == 0 ? juce::String ("0") : msText (ms), bx, (int) r.getBottom() + 4, 70, 16,
                    j == 0 ? juce::Justification::centredLeft : j == 4 ? juce::Justification::centredRight : juce::Justification::centred);
    }
    if (w.bins <= 1)
    {
        g.setColour (c::sub);
        g.setFont (font (15.0f));
        g.drawText (tr (en, "Kein Signal", "No signal"), r.toNearestInt(), juce::Justification::centred);
        return;
    }

    // Scale to the loudest sample shown; every bin draws the span from its lowest to its highest
    // sample (a filled band for long windows, a plain line when a bin holds a single sample).
    const bool show[] = { ! traceButtons[0].isVisible() || traceButtons[0].getToggleState(),
                          ! traceButtons[1].isVisible() || traceButtons[1].getToggleState() };
    float top = 0.05f;
    bool singleSamples = true;
    for (int i = 0; i < w.bins; ++i)
    {
        if (show[0])
            top = juce::jmax (top, std::abs (w.minA[(size_t) i]), std::abs (w.maxA[(size_t) i]));
        if (show[1])
            top = juce::jmax (top, std::abs (w.minB[(size_t) i]), std::abs (w.maxB[(size_t) i]));
        singleSamples = singleSamples && w.minA[(size_t) i] == w.maxA[(size_t) i];
    }
    auto xAt = [&r, &w] (int i) { return r.getX() + r.getWidth() * i / (float) (w.bins - 1); };
    auto yAt = [&r, top] (float v) { return r.getCentreY() - v / top * r.getHeight() * 0.5f; };

    for (int s = 0; s < 2; ++s)
    {
        if (! show[s])
            continue;
        const auto& lo = s == 0 ? w.minA : w.minB;
        const auto& hi = s == 0 ? w.maxA : w.maxB;
        const auto colour = s == 0 ? c::a : c::b;
        if (singleSamples)
        {
            juce::Path p;
            for (int i = 0; i < w.bins; ++i)
                if (i == 0) p.startNewSubPath (xAt (i), yAt (hi[(size_t) i])); else p.lineTo (xAt (i), yAt (hi[(size_t) i]));
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.5f));
            continue;
        }
        juce::Path band;
        band.startNewSubPath (xAt (0), yAt (hi[0]));
        for (int i = 1; i < w.bins; ++i)
            band.lineTo (xAt (i), yAt (hi[(size_t) i]));
        for (int i = w.bins - 1; i >= 0; --i)
            band.lineTo (xAt (i), yAt (lo[(size_t) i]));
        band.closeSubPath();
        g.setColour (colour.withAlpha (0.38f));
        g.fillPath (band);
        g.setColour (colour.withAlpha (0.85f));
        g.strokePath (band, juce::PathStrokeType (1.0f));
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
        // At or above full scale in red, true peak above -1 dBTP in amber.
        const double level = i == 0 ? lv.peak : i == 1 ? lv.truePeak : noLevel;
        g.setColour (level >= 0.0 ? c::bad : (i == 1 && level > -1.0) ? c::amber : c::text);
        g.setFont (font (19.0f));
        g.drawText (cells[i].value, in, juce::Justification::centredLeft);
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), cells[i].value);
        g.setColour (c::sub);
        g.setFont (font (12.5f));
        g.drawText (cells[i].unit, in.withTrimmedLeft (w + 5), juce::Justification::centredLeft);
    }

    // Clipping since reset: count of clips (runs at or above 0 dBFS) and clipped samples
    const auto clips = lv.clips();
    g.setColour (clips > 0 ? c::bad.withAlpha (0.18f) : c::field);
    g.fillRoundedRectangle (clipRect.toFloat(), 3.0f);
    g.setColour (clips > 0 ? c::bad : c::sub);
    g.setFont (font (13.5f));
    g.drawFittedText (clips > 0 ? "Clips " + juce::String (clips) + " " + juce::String::fromUTF8 ("·") + " " + juce::String (lv.clippedSamples) + " Samples"
                                : tr (en, "Clips 0", "Clips 0"),
                      clipRect.reduced (6, 0), juce::Justification::centred, 1, 0.8f);
}

void Panel::paintMeters (juce::Graphics& g, juce::Rectangle<int> r)
{
    const int bar = 17, gap = 9;
    const int x0 = r.getX() + 4;
    const int top = r.getY() + 20, bottom = r.getBottom() - 18, height = bottom - top;
    g.setFont (font (12.5f));
    const auto now = juce::Time::getMillisecondCounter();
    const bool clipLit[] = { now < clipLeftUntil, now < clipRightUntil };
    for (int s = 0; s < 2; ++s)
    {
        const auto label = juce::Rectangle<int> (x0 + s * (bar + gap), r.getY(), bar, 16);
        if (clipLit[s])
        {
            g.setColour (c::bad);
            g.fillRoundedRectangle (label.toFloat(), 2.0f);
        }
        g.setColour (clipLit[s] ? juce::Colour (0xff1d2226) : c::sub);
        g.drawText (s == 0 ? "L" : "R", label, juce::Justification::centred);
    }

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
    const int mode = historyMode();   // 0 correlation, 1 level, 2 loudness
    g.setColour (c::graph);
    g.fillRect (area);
    auto title = area.removeFromTop (32);
    g.setColour (c::strip);
    g.fillRect (title);
    g.setColour (c::text);
    g.setFont (font (14.0f));
    g.drawText ("HISTORY", title.reduced (13, 0), juce::Justification::centredLeft);

    // Legend of the two lines of this view, right of the view buttons
    const juce::String names[3][2] = {
        { tr (en, "L/R-Korrelation", "L/R correlation"), tr (en, "Mono-Score", "Mono score") },
        { "RMS", "Peak" },
        { tr (en, "LUFS kurz", "LUFS short"), tr (en, "LUFS momentan", "LUFS momentary") } };
    int lx = historyModeButtons[0].getRight() + 18;
    for (int line = 0; line < 2; ++line)
    {
        g.setColour (line == 0 ? c::a : c::b);
        g.fillRect (lx, title.getCentreY() - 1, 16, 2);
        g.setColour (c::sub);
        g.setFont (font (13.0f));
        g.drawText (names[mode][line], lx + 20, title.getY(), 120, title.getHeight(), juce::Justification::centredLeft);
        lx += 20 + juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), names[mode][line]) + 18;
    }
    if (mode != 0)
    {
        g.setColour (c::bad);
        g.fillRect (lx, title.getCentreY() - 4, 3, 8);
        g.setColour (c::sub);
        g.drawText ("Clip", lx + 8, title.getY(), 40, title.getHeight(), juce::Justification::centredLeft);
        lx += 52;
    }
    if (juce::Time::getMillisecondCounter() < gapShownUntil)
    {
        g.setColour (c::amber);
        g.drawText (tr (en, "Lücke: Analyse kam nicht hinterher", "Gap: the analysis could not keep up"), lx + 10, title.getY(), 260, title.getHeight(), juce::Justification::centredLeft);
    }

    auto r = area.toFloat().withTrimmedLeft (46).withTrimmedRight (18).withTrimmedTop (8).withTrimmedBottom (22);
    // 30/60/90 s: the newest 100 ms points; Song: one point per second since the last reset.
    const bool song = historySeconds == 0;
    const int count = song ? (int) songPoints.size() : historySeconds * 10;
    const History::Point* points = song ? songPoints.data() : snapshot->history.data() + (History::points - count);
    if (count < 2)
    {
        g.setColour (c::sub);
        g.setFont (font (14.0f));
        g.drawText (tr (en, "Noch kein Verlauf – er beginnt mit der Wiedergabe.", "No history yet – it starts with playback."),
                    r.toNearestInt(), juce::Justification::centred);
        return;
    }

    // Levels and loudness: the scale follows what is visible. The bottom sits a little below the
    // quiet end (the lowest 5 % – short silences – are ignored), in 6 dB steps, 24 … 60 dB span;
    // the top is 0 dB, or +6 dB when something went over.
    float top = 0.0f, bottom = -60.0f;
    if (mode == 0)
    {
        // Correlation: from a little below the lowest visible value (in 0.25 steps) up to +1 with some
        // headroom, so a mono signal does not hide on the top edge.
        float lowest = 1.0f;
        for (int i = 0; i < count; ++i)
            if (points[i].valid)
                lowest = std::min (lowest, points[i].value);
        top = 1.1f;
        bottom = juce::jlimit (-1.0f, 0.5f, std::floor ((lowest - 0.1f) / 0.25f) * 0.25f);
    }
    else
    {
        std::vector<float> values;
        values.reserve ((size_t) count * 2);
        for (int i = 0; i < count; ++i)
        {
            const auto& pt = points[i];
            for (float v : { mode == 1 ? pt.rms : pt.shortTerm, mode == 1 ? pt.peak : pt.momentary })
                if (v > -200.0f)
                    values.push_back (v);
        }
        if (! values.empty())
        {
            std::sort (values.begin(), values.end());
            const float quiet = values[(size_t) (values.size() / 20)];
            top = values.back() > 0.0f ? 6.0f : 0.0f;
            bottom = juce::jlimit (top - 60.0f, top - 24.0f, std::floor ((quiet - 3.0f) / 6.0f) * 6.0f);
        }
    }

    // Vertical scale: correlation -1 … +1, levels and loudness bottom … top dB
    auto yOf = [&r, top, bottom] (float v)
    {
        return r.getY() + juce::jlimit (0.0f, 1.0f, (top - v) / (top - bottom)) * r.getHeight();
    };
    // Mono score on its own axis (right): 0 … 110, so 100 sits just below the top.
    auto yOfScore = [&r] (float score) { return r.getY() + juce::jlimit (0.0f, 1.0f, (110.0f - score) / 110.0f) * r.getHeight(); };
    g.setFont (font (12.0f));
    if (mode == 0)
    {
        for (float v = 1.0f; v >= bottom - 0.01f; v -= 0.25f)
        {
            g.setColour (std::abs (v) < 0.01f ? c::bad.withAlpha (0.45f) : c::grid);   // below 0: working against each other
            g.drawHorizontalLine ((int) yOf (v), r.getX(), r.getRight());
        }
        const float middle = std::round ((1.0f + bottom) / 0.5f) * 0.25f;
        for (float v : { 1.0f, middle, bottom })
        {
            g.setColour (c::sub);
            g.drawText (number (v, 2, en, v > 0.0f), (int) r.getX() - 46, (int) yOf (v) - 7, 42, 14, juce::Justification::centredRight);
        }
        g.setColour (c::b.withAlpha (0.8f));
        for (int s : { 100, 50, 0 })
            g.drawText (juce::String (s), (int) r.getRight() - 30, (int) yOfScore ((float) s) - 7, 27, 14, juce::Justification::centredRight);
    }
    else
    {
        for (float v = top; v >= bottom - 0.01f; v -= 6.0f)
        {
            g.setColour (v == 0.0f && top > 0.0f ? c::bad.withAlpha (0.5f) : c::grid);
            g.drawHorizontalLine ((int) yOf (v), r.getX(), r.getRight());
        }
        const float middle = std::round ((top + bottom) / 12.0f) * 6.0f;
        for (float v : { top, middle, bottom })
        {
            g.setColour (c::sub);
            g.drawText (number (v, 0, en, v > 0.0f), (int) r.getX() - 44, (int) yOf (v) - 7, 38, 14, juce::Justification::centredRight);
        }
    }

    auto xOf = [&r, count] (int i) { return r.getX() + r.getWidth() * i / (float) (count - 1); };
    for (int j = 0; j <= 4; ++j)
    {
        const float x = r.getX() + r.getWidth() * j / 4.0f;
        g.setColour (c::grid);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        const int index = juce::jlimit (0, count - 1, (int) std::lround ((count - 1) * j / 4.0));
        juce::String label;
        if (song)
            label = juce::String (index / 60) + ":" + juce::String (index % 60).paddedLeft ('0', 2);   // one point per second
        else
        {
            const int ago = juce::roundToInt (historySeconds * (1.0 - j / 4.0));
            label = j == 4 ? tr (en, "Jetzt", "Now") : juce::String::fromUTF8 ("−") + juce::String (ago) + " s";
        }
        if (points[index].bar >= 1.0)
            label << tr (en, " · T. ", " · bar ") << juce::String ((int) std::floor (points[index].bar));
        g.setColour (c::sub);
        const int bx = j == 0 ? (int) x : j == 4 ? (int) x - 140 : (int) x - 70;
        g.drawText (label, bx, (int) r.getBottom() + 4, 140, 16,
                    j == 0 ? juce::Justification::centredLeft : j == 4 ? juce::Justification::centredRight : juce::Justification::centred);
    }

    // Value of a line at one point, or "no value"
    auto valueOf = [mode] (const History::Point& pt, int line, float& v)
    {
        if (mode == 0)
        {
            if (! pt.valid)
                return false;
            v = line == 0 ? pt.value : pt.second / 50.0f - 1.0f;
            return line == 0 || pt.second >= 0.0f;
        }
        v = mode == 1 ? (line == 0 ? pt.rms : pt.peak) : (line == 0 ? pt.shortTerm : pt.momentary);
        return v > -200.0f;
    };

    // Display smoothing only (the points keep their 100 ms values): a moving average over about
    // 0.6 s at 30 s, wider for longer views; the peak line follows a moving maximum first, so it
    // stays an honest envelope of the peaks.
    const int half = song ? 1 : historySeconds <= 30 ? 3 : historySeconds <= 60 ? 5 : 7;
    std::vector<float> raw ((size_t) count), shaped ((size_t) count);
    std::vector<char> present ((size_t) count);
    for (int line = 0; line < 2; ++line)
    {
        for (int i = 0; i < count; ++i)
        {
            float v = 0.0f;
            present[(size_t) i] = valueOf (points[i], line, v) ? 1 : 0;
            raw[(size_t) i] = v;
        }
        const bool envelope = mode == 1 && line == 1;
        if (envelope)
        {
            for (int i = 0; i < count; ++i)
            {
                float m = -1000.0f;
                for (int k = std::max (0, i - half); k <= std::min (count - 1, i + half); ++k)
                    if (present[(size_t) k])
                        m = std::max (m, raw[(size_t) k]);
                shaped[(size_t) i] = m;
            }
            raw = shaped;
        }
        for (int i = 0; i < count; ++i)
        {
            double sum = 0.0;
            int n = 0;
            for (int k = std::max (0, i - half); k <= std::min (count - 1, i + half); ++k)
                if (present[(size_t) k])
                {
                    sum += raw[(size_t) k];
                    ++n;
                }
            shaped[(size_t) i] = n > 0 ? (float) (sum / n) : 0.0f;
        }

        juce::Path p;
        bool started = false;
        const bool scoreLine = mode == 0 && line == 1;
        for (int i = 0; i < count; ++i)
        {
            if (! present[(size_t) i]) { started = false; continue; }
            const float v = shaped[(size_t) i];
            const float y = scoreLine ? yOfScore ((v + 1.0f) * 50.0f) : yOf (v);
            if (! started) p.startNewSubPath (xOf (i), y); else p.lineTo (xOf (i), y);
            started = true;
        }
        // Like a loudness meter's timeline: short-term loudness and the peak envelope as filled areas.
        const bool filled = (mode == 2 && line == 0) || (mode == 1 && line == 1);
        if (filled)
        {
            juce::Path fill;
            bool open = false;
            int last = 0;
            for (int i = 0; i <= count; ++i)
            {
                const bool here = i < count && present[(size_t) i];
                if (here && ! open)
                {
                    fill.startNewSubPath (xOf (i), r.getBottom());
                    open = true;
                }
                if (here)
                {
                    fill.lineTo (xOf (i), yOf (shaped[(size_t) i]));
                    last = i;
                }
                if (! here && open)
                {
                    fill.lineTo (xOf (last), r.getBottom());
                    fill.closeSubPath();
                    open = false;
                }
            }
            const auto colour = line == 0 ? c::a : c::b;
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.55f), 0, r.getY(), colour.withAlpha (0.12f), 0, r.getBottom(), false));
            g.fillPath (fill);
        }
        g.setColour (line == 0 ? c::a : c::b.withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (line == 0 ? 1.7f : 1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (mode == 2 && snapshot->levels.integrated > noLevel + 1.0)
    {
        // Integrated loudness since reset as a dashed line with its value.
        const float y = yOf ((float) snapshot->levels.integrated);
        const float dashes[] = { 6.0f, 4.0f };
        g.setColour (c::text.withAlpha (0.7f));
        g.drawDashedLine (juce::Line<float> (r.getX(), y, r.getRight(), y), dashes, 2, 1.2f);
        g.setFont (font (12.5f));
        g.drawText (tr (en, "Integriert ", "Integrated ") + number (snapshot->levels.integrated, 1, en) + " LUFS",
                    (int) r.getX() + 6, (int) y - 16, 200, 14, juce::Justification::centredLeft);
    }

    if (mode != 0)
    {
        // Clipping in red: a mark at the top for every point with a sample at or above 0 dBFS.
        g.setColour (c::bad);
        const float w = juce::jlimit (2.0f, 3.0f, r.getWidth() / count);   // thin marks, also with few points
        for (int i = 0; i < count; ++i)
            if (points[i].clipped)
                g.fillRect (xOf (i) - w * 0.5f, r.getY() - 6.0f, w, r.getHeight() * 0.25f + 6.0f);
    }
}

void Panel::paintAdvice (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& advice = shownAdvice;
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
