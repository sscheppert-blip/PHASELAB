#include "shared/BusLink.h"

namespace phaselab
{

BusLink::BusLink (std::atomic<float>& groupParameter, std::atomic<float>& cutoffParameter)
    : juce::Thread ("PhaseLab Bus Link"), group (groupParameter), cutoff (cutoffParameter),
      lastWave (std::make_unique<WaveformBins>()),
      work (std::make_unique<BusLinkStatus>()), status (std::make_unique<BusLinkStatus>())
{
}

BusLink::~BusLink()
{
    stop();
}

void BusLink::start()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void BusLink::stop()
{
    stopThread (1000);
}

void BusLink::run()
{
    while (! threadShouldExit())
    {
        step (link::nowMs());
        wait (50);
    }
}

BusLinkStatus BusLink::getStatus() const
{
    const juce::SpinLock::ScopedLockType lock (statusLock);
    return *status;
}

void BusLink::getStatus (BusLinkStatus& out) const
{
    const juce::SpinLock::ScopedLockType lock (statusLock);
    out = *status;
}

void BusLink::step (std::int64_t now)
{
    const int g = juce::jlimit (0, link::maxGroups - 1, juce::roundToInt (group.load()));
    if (g != currentGroup)
    {
        trackerA.reset();
        trackerB.reset();
        currentGroup = g;
        wasValid = false;
    }

    auto& next = *work;
    next.state = LinkState::disconnected;
    next.result = {};
    next.spectrum = {};
    next.wave.bins = 0;
    next.group = g;

    auto* slotA = slots.acquire (g, 0);
    auto* slotB = slots.acquire (g, 1);
    if (slotA != nullptr)
        trackerA.poll (*slotA, now);
    if (slotB != nullptr)
        trackerB.poll (*slotB, now);

    const auto& a = trackerA.status();
    const auto& b = trackerB.status();
    next.a = { slotA != nullptr && a.connected, a.stale, a.name };
    next.b = { slotB != nullptr && b.connected, b.stale, b.name };
    next.sampleRate = a.sampleRate;

    auto decide = [&]() -> LinkState
    {
        if (! next.a.connected || ! next.b.connected)
            return LinkState::disconnected;
        if (a.stale || b.stale)
            return LinkState::stale;
        if (a.offline || b.offline)
            return LinkState::offline;
        if (! a.hostTime || ! b.hostTime)
            return LinkState::noHostTime;
        if (a.sampleRate != b.sampleRate || a.sampleRate <= 0.0)
            return LinkState::rateMismatch;

        // The newest stretch both sources cover, each from one continuous run.
        const int length = (int) std::lround (windowSeconds * a.sampleRate);
        const std::int64_t end = std::min (a.latestEnd, b.latestEnd);
        const std::int64_t start = end - length;
        windowA.resize ((size_t) length);
        windowB.resize ((size_t) length);

        std::int64_t firstA = 0, lastA = 0, firstB = 0, lastB = 0;
        const bool completeA = trackerA.buffer().copy (start, length, a.latestKey, windowA.data(), firstA, lastA);
        const bool completeB = trackerB.buffer().copy (start, length, b.latestKey, windowB.data(), firstB, lastB);
        if (! completeA || ! completeB)
            return wasValid ? LinkState::gap : LinkState::acquiring;

        // Same timeline position but written far apart in time: one side is from an earlier pass
        // (loop) or lags far behind. Never compare that.
        if (std::abs (lastA - lastB) > maxWriteSkewMs || std::abs (firstA - firstB) > maxWriteSkewMs)
            return LinkState::acquiring;

        // About ten analyses per second are enough for the display; the offset search is the costly part.
        const float cut = cutoff.load();
        const double zoom = waveSeconds.load();
        if (! wasValid || std::abs (end - lastAnalysedEnd) >= (std::int64_t) (0.1 * a.sampleRate) || cut != lastCutoff || zoom != lastZoom)
        {
            lastResult = analyzer.analyse (windowA.data(), windowB.data(), length, a.sampleRate, cut);
            lastAnalysedEnd = end;
            lastCutoff = cut;
            lastZoom = zoom;

            if (displayUsers.load() > 0)
            {
                spectrum.prepare (a.sampleRate);
                spectrum.analyseStretch (windowA.data(), windowB.data(), length);
                lastSpectrum = spectrum.result();
                const double seconds = juce::jlimit (waveformMinSeconds, waveformMaxSeconds, waveSeconds.load());
                const int count = std::min (length, std::max (2, (int) std::lround (seconds * a.sampleRate)));
                const int back = waveTrigger.load() ? std::min (length - count, (int) (0.05 * a.sampleRate)) : 0;
                lastWave->fill (windowA.data(), windowB.data(), length, length, count, a.sampleRate, back);
            }
            else
            {
                lastSpectrum = {};
                lastWave->bins = 0;
            }
        }
        next.result = lastResult;
        next.spectrum = lastSpectrum;
        next.wave = *lastWave;
        return LinkState::valid;
    };

    next.state = decide();
    wasValid = next.state == LinkState::valid || (wasValid && next.state == LinkState::gap);

    const juce::SpinLock::ScopedLockType lock (statusLock);
    *status = next;
}

} // namespace phaselab
