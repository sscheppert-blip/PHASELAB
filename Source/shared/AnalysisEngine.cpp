#include "shared/AnalysisEngine.h"

namespace phaselab
{

AnalysisEngine::AnalysisEngine (const MonoScoreMeter& monoMeter)
    : juce::Thread ("PhaseLab Analysis"), mono (monoMeter),
      frameLeft ((size_t) spectrumSize), frameRight ((size_t) spectrumSize),
      ringLeft ((size_t) waveformCapacity, 0.0f), ringRight ((size_t) waveformCapacity, 0.0f),
      building (std::make_unique<EngineSnapshot>()), snapshot (std::make_unique<EngineSnapshot>())
{
    longHistory.reserve ((size_t) longHistoryCapacity);
}

void WaveformBins::fill (const float* a, const float* b, int capacity, int end, int count, double rate, int maxBack) noexcept
{
    count = std::clamp (count, 0, capacity);
    maxBack = std::clamp (maxBack, 0, capacity - count);
    triggered = false;
    auto at = [capacity] (const float* x, int i) { return x[((i % capacity) + capacity) % capacity]; };

    if (maxBack > 0 && rate > 0.0)
    {
        // Two one-pole low-passes at 150 Hz: the trigger follows the bass, not the hiss.
        const float k = 1.0f - std::exp (-2.0f * 3.14159265f * 150.0f / (float) rate);
        const int latest = end - count, earliest = latest - maxBack;
        const int warm = std::min ((int) (0.01 * rate), capacity - count - maxBack);
        float y1 = 0.0f, y2 = 0.0f, previous = 0.0f, level = 0.0f;
        int found = -1;
        for (int i = earliest - warm; i <= latest; ++i)
        {
            y1 += k * (at (a, i) - y1);
            y2 += k * (y1 - y2);
            if (i > earliest)
            {
                level = std::max (level, std::abs (y2));
                if (previous < 0.0f && y2 >= 0.0f)
                    found = i;   // keep the latest crossing: freshest picture
            }
            previous = y2;
        }
        if (found >= 0 && level > 1.0e-3f)
        {
            // The filters lag: step back to the real rising zero crossing just before (within 10 ms).
            for (int i = found; i > std::max (earliest, found - (int) (0.01 * rate)); --i)
                if (at (a, i - 1) < 0.0f && at (a, i) >= 0.0f)
                {
                    found = i;
                    break;
                }
            end = found + count;
            triggered = true;
        }
    }

    bins = std::min (waveformBins, count);
    seconds = rate > 0.0 ? count / rate : 0.0;
    for (int bin = 0; bin < bins; ++bin)
    {
        const int from = (int) ((long long) bin * count / bins), to = std::max (from + 1, (int) ((long long) (bin + 1) * count / bins));
        float loA = 1.0e9f, hiA = -1.0e9f, loB = 1.0e9f, hiB = -1.0e9f;
        for (int i = from; i < to; ++i)
        {
            const int j = ((end - count + i) % capacity + capacity) % capacity;
            loA = std::min (loA, a[j]); hiA = std::max (hiA, a[j]);
            loB = std::min (loB, b[j]); hiB = std::max (hiB, b[j]);
        }
        minA[(size_t) bin] = loA; maxA[(size_t) bin] = hiA;
        minB[(size_t) bin] = loB; maxB[(size_t) bin] = hiB;
    }
}

void AnalysisEngine::copyLongHistory (std::vector<History::Point>& out) const
{
    const juce::SpinLock::ScopedLockType lock (longLock);
    out = longHistory;   // the caller keeps its vector, so this reuses its memory
}

void AnalysisEngine::addToLongHistory (const History::Point& p, bool recording)
{
    if (! recording)
        return;
    if (longCount == 0)
        longAccumulator = {};
    ++longCount;
    if (p.valid)
    {
        ++longValid;
        longCorrelation += p.value;
        longAccumulator.valid = true;
    }
    if (p.second >= 0.0f) { longScore += p.second; ++longScoreCount; }
    if (p.rms > -200.0f) { longRms += p.rms; ++longRmsCount; }
    longAccumulator.peak = std::max (longAccumulator.peak, p.peak);
    longAccumulator.momentary = std::max (longAccumulator.momentary, p.momentary);
    longAccumulator.shortTerm = p.shortTerm;
    longAccumulator.clipped = longAccumulator.clipped || p.clipped;
    if (longAccumulator.bar < 0.0)
        longAccumulator.bar = p.bar;

    if (longCount < 10)
        return;
    auto point = longAccumulator;
    point.value = longValid > 0 ? (float) (longCorrelation / longValid) : 0.0f;
    point.second = longScoreCount > 0 ? (float) (longScore / longScoreCount) : -1.0f;
    point.rms = longRmsCount > 0 ? (float) (longRms / longRmsCount) : (float) noLevel;
    longCount = longValid = longScoreCount = longRmsCount = 0;
    longCorrelation = longScore = longRms = 0.0;
    {
        const juce::SpinLock::ScopedLockType lock (longLock);
        if ((int) longHistory.size() >= longHistoryCapacity)
            longHistory.erase (longHistory.begin());   // once a second, after two hours: acceptable
        longHistory.push_back (point);
    }
    ++longVersion;
}

AnalysisEngine::~AnalysisEngine()
{
    stop();
}

void AnalysisEngine::prepare (double sampleRate)
{
    const bool wasRunning = isThreadRunning();
    stop();
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    fifo.reset();
    levels.prepare (rate);
    stereo.prepare (rate);
    spectrum.prepare (rate);
    history.clear();
    {
        const juce::SpinLock::ScopedLockType lock (longLock);
        longHistory.clear();
    }
    longCount = 0;
    ++longVersion;
    frameFill = 0;
    std::fill (ringLeft.begin(), ringLeft.end(), 0.0f);
    std::fill (ringRight.begin(), ringRight.end(), 0.0f);
    ringPosition = 0;
    historyCountdown = 0;
    prepared = true;
    if (wasRunning)
        start();
}

void AnalysisEngine::start()
{
    if (prepared && ! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void AnalysisEngine::stop()
{
    stopThread (1000);
}

void AnalysisEngine::setDisplayActive (bool active) noexcept
{
    displayUsers.fetch_add (active ? 1 : -1);
}

void AnalysisEngine::push (const float* left, const float* right, int numSamples, const BlockInfo& info) noexcept
{
    latestPlaying.store (info.playing, std::memory_order_relaxed);
    latestHasPosition.store (info.hasMusicalPosition, std::memory_order_relaxed);
    latestPpq.store (info.ppq, std::memory_order_relaxed);
    latestNumerator.store (info.numerator, std::memory_order_relaxed);
    latestDenominator.store (info.denominator, std::memory_order_relaxed);

    if (fifo.getFreeSpace() < numSamples)
    {
        overflowed.store (true);   // never wait for the worker
        return;
    }
    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    if (size1 > 0)
    {
        fifoBuffer.copyFrom (0, start1, left, size1);
        fifoBuffer.copyFrom (1, start1, right, size1);
    }
    if (size2 > 0)
    {
        fifoBuffer.copyFrom (0, start2, left + size1, size2);
        fifoBuffer.copyFrom (1, start2, right + size1, size2);
    }
    fifo.finishedWrite (size1 + size2);
}

void AnalysisEngine::run()
{
    while (! threadShouldExit())
    {
        processPending();
        wait (40);
    }
}

void AnalysisEngine::processPending()
{
    if (! prepared)
        return;

    if (resetRequested.exchange (false))
    {
        levels.reset();
        history.clear();
        {
            const juce::SpinLock::ScopedLockType lock (longLock);
            longHistory.clear();
        }
        longCount = 0;
        ++longVersion;
    }

    const bool display = displayUsers.load() > 0;
    if (display && ! displayWasActive)
    {
        spectrum.reset();   // fill anew instead of showing a stale picture (D-29)
        stereo.reset();
        frameFill = 0;
    }
    displayWasActive = display;

    const bool hadOverflow = overflowed.exchange (false);
    if (hadOverflow)
        overflowSinceLastPoint = true;

    chunkPeakLeft = chunkPeakRight = 0.0f;
    chunkClipLeft = chunkClipRight = false;
    for (;;)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0)
            break;
        int start1, size1, start2, size2;
        fifo.prepareToRead (std::min (ready, 2048), start1, size1, start2, size2);
        if (size1 > 0)
            processChunk (fifoBuffer.getReadPointer (0, start1), fifoBuffer.getReadPointer (1, start1), size1);
        if (size2 > 0)
            processChunk (fifoBuffer.getReadPointer (0, start2), fifoBuffer.getReadPointer (1, start2), size2);
        fifo.finishedRead (size1 + size2);
    }
    publish (hadOverflow);
}

void AnalysisEngine::processChunk (const float* left, const float* right, int n)
{
    const bool display = displayWasActive;
    levels.process (left, right, n);
    stereo.process (left, right, n);   // also feeds the goniometer; cheap

    for (int i = 0; i < n; ++i)
    {
        chunkPeakLeft = std::max (chunkPeakLeft, std::abs (left[i]));
        chunkPeakRight = std::max (chunkPeakRight, std::abs (right[i]));
        pointPeak = std::max (pointPeak, std::max (std::abs (left[i]), std::abs (right[i])));
        chunkClipLeft = chunkClipLeft || std::abs (left[i]) >= 1.0f;
        chunkClipRight = chunkClipRight || std::abs (right[i]) >= 1.0f;
        ringLeft[(size_t) ringPosition] = left[i];
        ringRight[(size_t) ringPosition] = right[i];
        ringPosition = (ringPosition + 1) % waveformCapacity;

        if (display)
        {
            frameLeft[(size_t) frameFill] = left[i];
            frameRight[(size_t) frameFill] = right[i];
            if (++frameFill == spectrumSize)
            {
                spectrum.addFrame (frameLeft.data(), frameRight.data(), 0.8);   // about 100 ms: calm, still follows the music
                // hop of a quarter frame: keep the newest three quarters
                const int keep = spectrumSize - spectrumSize / 4;
                std::copy (frameLeft.begin() + spectrumSize / 4, frameLeft.end(), frameLeft.begin());
                std::copy (frameRight.begin() + spectrumSize / 4, frameRight.end(), frameRight.begin());
                frameFill = keep;
            }
        }

        // History: one point per 100 ms (R-13), with the bar position when the host provides it (D-27).
        if (--historyCountdown <= 0)
        {
            historyCountdown = std::max (1, (int) std::lround (0.1 * rate));
            const auto st = stereo.values();
            History::Point p;
            p.valid = st.valid && ! overflowSinceLastPoint;
            p.value = (float) st.correlation;
            p.second = (float) mono.fullScore();
            if (latestHasPosition.load() && latestPlaying.load())
            {
                const double barLength = 4.0 * latestNumerator.load() / std::max (1, latestDenominator.load());
                p.bar = latestPpq.load() / barLength + 1.0;
            }
            const auto lv = levels.values();
            p.peak = pointPeak > 0.0f ? 20.0f * std::log10 (pointPeak) : (float) noLevel;
            p.rms = (float) lv.rms;
            p.momentary = (float) lv.momentary;
            p.shortTerm = (float) lv.shortTerm;
            p.clipped = pointPeak >= 1.0f;
            pointPeak = 0.0f;
            history.push (p);
            // Record while the host plays; hosts without a transport: while there is signal.
            addToLongHistory (p, latestPlaying.load() || (! latestHasPosition.load() && st.valid));
            overflowSinceLastPoint = false;
        }
    }
}

void AnalysisEngine::publish (bool hadOverflow)
{
    // Built outside the lock, then copied in.
    auto* next = building.get();
    next->levels = levels.values();
    next->stereo = stereo.values();
    next->display = displayWasActive;
    if (displayWasActive)
    {
        next->spectrum = spectrum.result();
        stereo.copyScope (next->scopeMid, next->scopeSide);
        const double seconds = juce::jlimit (waveformMinSeconds, waveformMaxSeconds, waveSeconds.load());
        const int count = std::min (waveformCapacity, std::max (2, (int) std::lround (seconds * rate)));
        const int back = waveTrigger.load() ? (int) (0.05 * rate) : 0;   // up to one 20 Hz cycle earlier
        next->wave.fill (ringLeft.data(), ringRight.data(), waveformCapacity, ringPosition, count, rate, back);
    }
    else
    {
        next->spectrum = {};
        next->wave.bins = 0;
    }
    next->meterLeft = chunkPeakLeft;
    next->meterRight = chunkPeakRight;
    next->clipLeft = chunkClipLeft;
    next->clipRight = chunkClipRight;
    history.copy (next->history.data(), History::points);
    next->overflow = hadOverflow;

    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    *snapshot = *next;
}

void AnalysisEngine::copySnapshot (EngineSnapshot& out) const
{
    const juce::SpinLock::ScopedLockType lock (snapshotLock);
    out = *snapshot;
}

} // namespace phaselab
