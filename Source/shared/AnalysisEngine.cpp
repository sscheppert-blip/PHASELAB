#include "shared/AnalysisEngine.h"

namespace phaselab
{

AnalysisEngine::AnalysisEngine (const MonoScoreMeter& monoMeter)
    : juce::Thread ("PhaseLab Analysis"), mono (monoMeter),
      frameLeft ((size_t) spectrumSize), frameRight ((size_t) spectrumSize),
      ringLeft ((size_t) waveformCapacity, 0.0f), ringRight ((size_t) waveformCapacity, 0.0f),
      building (std::make_unique<EngineSnapshot>()), snapshot (std::make_unique<EngineSnapshot>())
{
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
        ringLeft[(size_t) ringPosition] = left[i];
        ringRight[(size_t) ringPosition] = right[i];
        ringPosition = (ringPosition + 1) % waveformCapacity;

        if (display)
        {
            frameLeft[(size_t) frameFill] = left[i];
            frameRight[(size_t) frameFill] = right[i];
            if (++frameFill == spectrumSize)
            {
                spectrum.addFrame (frameLeft.data(), frameRight.data(), 0.6);
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
            history.push (p);
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
        next->waveLength = std::min (waveformCapacity, (int) std::lround (0.040 * rate));
        for (int i = 0; i < next->waveLength; ++i)
        {
            const int j = (ringPosition - next->waveLength + i + waveformCapacity) % waveformCapacity;
            next->waveLeft[(size_t) i] = ringLeft[(size_t) j];
            next->waveRight[(size_t) i] = ringRight[(size_t) j];
        }
    }
    else
    {
        next->spectrum = {};
        next->waveLength = 0;
    }
    next->meterLeft = chunkPeakLeft;
    next->meterRight = chunkPeakRight;
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
