#include "ui/Advice.h"

#include "ui/Look.h"

namespace phaselab::ui
{

juce::String linkStateText (LinkState s, bool en)
{
    switch (s)
    {
        case LinkState::disconnected: return tr (en, "Quelle fehlt · Channel Analyzer auf Senden, gleiche Gruppe", "Source missing · Channel Analyzer set to send, same group");
        case LinkState::stale:        return tr (en, "Quelle liefert keine Daten (Wiedergabe gestoppt?)", "Source sends no data (playback stopped?)");
        case LinkState::offline:      return tr (en, "Offline-Rendern · A/B-Vergleich gesperrt", "Offline render · A/B comparison locked");
        case LinkState::noHostTime:   return tr (en, "Wiedergabe starten · ohne Timeline kein A/B-Vergleich", "Start playback · no A/B comparison without a timeline");
        case LinkState::rateMismatch: return tr (en, "Samplerate von A und B verschieden", "A and B run at different sample rates");
        case LinkState::acquiring:    return tr (en, "Synchronisiere …", "Synchronising …");
        case LinkState::gap:          return tr (en, "Lücke in den Daten · Vergleich ausgesetzt", "Gap in the data · comparison paused");
        case LinkState::valid:        return tr (en, "SYNC · Vergleich gültig", "SYNC · comparison valid");
    }
    return {};
}

juce::String offsetText (const ABResult& r, double rate, bool en)
{
    if (! r.enoughSignal)
        return tr (en, "Zu wenig Signal auf A oder B.", "Too little signal on A or B.");
    if (! r.clearOffset)
        return tr (en, "Kein eindeutiger Versatz · A und B ähneln sich wenig (bei Kick und Bass normal).",
                   "No clear offset · A and B have little in common (normal for kick and bass).");

    const int n = std::abs (r.offsetSamples);
    const auto count = juce::String (n);
    const auto ms = number (1000.0 * n / rate, 2, en);
    juce::String hint;
    if (n == 0)
        hint = tr (en, "Kein Versatz zwischen A und B.", "No offset between A and B.");
    else if (r.offsetSamples > 0)
        hint = tr (en, "B liegt ", "B is ") + count + tr (en, " Samples (", " samples (") + ms + tr (en, " ms) hinter A – Spur B um ", " ms) behind A – move track B ")
             + count + tr (en, " Samples vorziehen und erneut prüfen.", " samples earlier and check again.");
    else
        hint = tr (en, "B liegt ", "B is ") + count + tr (en, " Samples (", " samples (") + ms + tr (en, " ms) vor A – Spur B um ", " ms) ahead of A – move track B ")
             + count + tr (en, " Samples später setzen und erneut prüfen.", " samples later and check again.");

    if (r.polarityHint)
        hint << tr (en, " Polarität von B umkehren würde die Korrelation von ", " Inverting the polarity of B would bring the correlation from ")
             << number (r.offsetCorrelation, 2, en, true) << tr (en, " auf ", " to ") << number (-r.offsetCorrelation, 2, en, true)
             << tr (en, " bringen.", ".");
    return hint;
}

Advice adviseOwnInput (const OwnInput& in, bool en)
{
    const auto& s = in.snapshot;
    if (! s.stereo.valid)
        return { tr (en, "Kein Signal", "No signal"), tr (en, "Sobald Audio anliegt, erscheinen hier Hinweise.", "Hints appear here as soon as audio arrives."), false, true };

    // Clipping in the last 3 seconds comes first.
    bool recentClip = false;
    for (int i = History::points - 30; i < History::points; ++i)
        recentClip = recentClip || s.history[(size_t) i].clipped;
    if (recentClip)
        return { tr (en, "Clipping: ", "Clipping: ") + juce::String (s.levels.clips()) + tr (en, " Clips seit Reset", " clips since reset"),
                 tr (en, "Das Signal erreicht 0 dBFS (", "The signal reaches 0 dBFS (") + juce::String (s.levels.clippedSamples)
                     + tr (en, " Samples) – Pegel vor diesem Plugin senken. „Messung zurücksetzen“ setzt den Zähler zurück.",
                           " samples) – lower the level before this plugin. “Reset measurement” clears the count."), true };

    if (s.levels.truePeak > -1.0)
        return { tr (en, "True Peak über −1 dBTP", "True peak above −1 dBTP"),
                 tr (en, "Beim Umwandeln in MP3/AAC kann es clippen – den Pegel am Ende der Kette etwas senken.",
                     "Converting to MP3/AAC may clip – lower the level at the end of the chain a little."), true };

    if (in.bandScore >= 0 && in.bandScore < 70)
        return { tr (en, "Unter ", "Below ") + frequency (in.cutoff, en) + tr (en, " nicht monotauglich (", " not mono-safe (") + juce::String (in.bandScore) + "/100)",
                 tr (en, "Stereo-Verbreiterung, Chorus oder Phasenversatz im Bass prüfen und mit MONO abhören.",
                     "Check stereo widening, chorus or phase offset in the low end and listen with MONO."), true };

    if (s.spectrum.valid)
    {
        int worst = -1;
        for (int b = 3; b < bandCount; ++b)
            if (s.spectrum.monoLoss[(size_t) b] > noBand && s.spectrum.monoLoss[(size_t) b] < -6.0
                && (worst < 0 || s.spectrum.monoLoss[(size_t) b] < s.spectrum.monoLoss[(size_t) worst]))
                worst = b;
        if (worst >= 0)
            return { tr (en, "Mono-Verlust bei ", "Mono loss at ") + frequency (bandCentre (worst), en) + " (" + number (s.spectrum.monoLoss[(size_t) worst], 0, en) + " dB)",
                     tr (en, "In Mono wird es hier deutlich leiser. Stereo-Effekte und Polarität prüfen, die Stelle mit MONO abhören.",
                         "This range gets clearly quieter in mono. Check stereo effects and polarity, listen with MONO."), true };
    }

    if (s.stereo.correlation < 0.0)
        return { tr (en, "Negative L/R-Korrelation", "Negative L/R correlation"),
                 tr (en, "Links und rechts arbeiten gegeneinander – Polarität eines Kanals oder starke Stereo-Effekte prüfen.",
                     "Left and right work against each other – check the polarity of one channel or strong stereo effects."), true };

    // Exactly mono (L = R): the track's pan acts after the insert, so the stereo views stay centred.
    if (s.stereo.correlation > 0.9999 && s.stereo.sidePercent < 0.01)
        return { tr (en, "Spur ist mono (L = R)", "Track is mono (L = R)"),
                 tr (en, "Stereo-Ansichten bleiben in der Mitte: Das Panning der DAW wirkt erst nach den Insert-Plugins. Im Bus Analyzer siehst du die gepannte Summe.",
                     "The stereo views stay centred: the DAW pans after the insert plugins. The Bus Analyzer shows the panned sum."), false };

    if (s.spectrum.valid && s.spectrum.resonanceHz > 0.0)
        return { tr (en, "Schmale Überhöhung bei ", "Narrow peak at ") + frequency (s.spectrum.resonanceHz, en) + " (+" + number (s.spectrum.resonanceDb, 1, en) + " dB)",
                 tr (en, "Bereich gezielt abhören. Kann zum Klang gehören.", "Listen to this range on its own. It may be part of the sound."), false };

    return { tr (en, "Unauffällig", "Nothing striking"),
             tr (en, "Mono-Score ", "Mono score ") + (in.fullScore >= 0 ? juce::String (in.fullScore) : tr (en, "–", "–"))
                 + tr (en, "/100. Die Werte sind Hinweise, kein Qualitätsurteil.", "/100. The values are hints, not a verdict on quality."), false };
}

Advice adviseBus (const OwnInput& in, const BusLinkStatus& link, bool en)
{
    if (link.state != LinkState::valid)
    {
        if (link.state == LinkState::disconnected && ! link.a.connected && ! link.b.connected)
            return adviseOwnInput (in, en);
        return { linkStateText (link.state, en),
                 tr (en, "Zeitkritische A/B-Werte werden erst angezeigt, wenn der Vergleich gültig ist.",
                     "Time-critical A/B values are shown only while the comparison is valid."), false, true };
    }

    const auto& r = link.result;
    if (r.enoughSignal && r.clearOffset && (r.offsetSamples != 0 || r.polarityHint))
        return { r.polarityHint ? tr (en, "Polarität A/B gegensätzlich", "A and B in opposite polarity")
                                : tr (en, "Versatz zwischen A und B", "Offset between A and B"),
                 offsetText (r, link.sampleRate, en), true };

    if (link.spectrum.valid)
    {
        int worst = -1;
        for (int b = 2; b < 14; ++b)   // up to about 400 Hz
            if (link.spectrum.correlation[(size_t) b] > noBand && link.spectrum.correlation[(size_t) b] < -0.3
                && (worst < 0 || link.spectrum.correlation[(size_t) b] < link.spectrum.correlation[(size_t) worst]))
                worst = b;
        if (worst >= 0)
            return { frequency (bandCentre (worst), en) + tr (en, " · A und B löschen sich teilweise aus", " · A and B partly cancel"),
                     tr (en, "A und B gemeinsam im Mix und mit MONO abhören; Versatz, Polarität und Stimmung prüfen.",
                         "Listen to A and B together in the mix and with MONO; check offset, polarity and tuning."), true };
    }

    if (r.sumScore >= 0 && r.sumScore < 60)
        return { tr (en, "A+B addieren sich schlecht unter ", "A+B add up poorly below ") + frequency (in.cutoff, en) + " (" + juce::String (r.sumScore) + "/100)",
                 tr (en, "Im Bassbereich verlieren A und B zusammen Energie. Versatz, Polarität oder Überlappung der Grundtöne prüfen.",
                     "A and B lose energy together in the low end. Check offset, polarity or overlapping fundamentals."), true };

    auto own = adviseOwnInput (in, en);
    if (! own.warning)
        own = { tr (en, "A und B passen zusammen", "A and B fit together"),
                offsetText (r, link.sampleRate, en) + tr (en, " Summen-Score ", " Sum score ") + juce::String (r.sumScore) + "/100.", false };
    return own;
}

} // namespace phaselab::ui
