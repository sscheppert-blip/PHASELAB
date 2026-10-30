#pragma once

#include "core/Loudness.h"
#include "core/MonoScore.h"
#include "core/Spectrum.h"
#include "core/Stereo.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <vector>

namespace phaselab
{

// Host position of one block, as far as the host provides it.
struct BlockInfo
{
    bool playing = false;
    bool hasMusicalPosition = false;
    double ppq = 0.0;
    int numerator = 4, denominator = 4;
};

inline constexpr int waveformCapacity = 1 << 16;   // up to 400 ms at 96 kHz (and more at 48 kHz)
inline constexpr int waveformBins = 1024;          // display points: min/max per point
inline constexpr double waveformMinSeconds = 0.002, waveformMaxSeconds = 0.4;

// Waveform of two signals for display: per bin the lowest and highest sample, so long windows read
// like a DAW waveform and short ones show the single samples.
struct WaveformBins
{
    std::array<float, waveformBins> minA {}, maxA {}, minB {}, maxB {};
    int bins = 0;              // 0 = nothing to show
    double seconds = 0.0;      // length of the shown window

    // Fills from the newest `count` samples ending at `end` of two buffers of `capacity` (ring if wrap).
    // With maxBack > 0 the window may start up to maxBack samples earlier, at the latest rising zero
    // crossing of the low end of `a` – like an oscilloscope trigger, so periodic signals stand still.
    void fill (const float* a, const float* b, int capacity, int end, int count, double rate, int maxBack = 0) noexcept;
    bool triggered = false;   // the window starts at a trigger point
};

// What the editor shows of the plugin's own input. Copied as a whole under a spin lock.
struct EngineSnapshot
{
    LevelValues levels;
    StereoValues stereo;
    SpectrumResult spectrum;
    std::array<float, StereoMeter::scopePoints> scopeMid {}, scopeSide {};
    WaveformBins wave;   // L (a) and R (b)
    float meterLeft = 0.0f, meterRight = 0.0f;   // peak of the newest chunk
    bool clipLeft = false, clipRight = false;    // a sample at or above 0 dBFS in the newest chunk
    std::array<History::Point, History::points> history {};
    bool overflow = false;                       // the worker could not keep up since the last snapshot
    bool display = false;                        // spectrum, scope and waveform are being computed
};

// Worker thread for the analysis of the plugin's own input (docs/spec/03): the audio thread only
// copies into a lock-free FIFO; metering, spectrum, stereo, history and waveform run here. Display-only
// work (FFT, goniometer, waveform) runs only while an editor is open (D-29).
class AnalysisEngine : private juce::Thread
{
public:
    explicit AnalysisEngine (const MonoScoreMeter& monoMeter);
    ~AnalysisEngine() override;

    void prepare (double sampleRate);   // not realtime: allocates
    void start();
    void stop();

    // Audio thread.
    void push (const float* left, const float* right, int numSamples, const BlockInfo& info) noexcept;

    // Worker (or tests): processes everything queued so far.
    void processPending();

    void setDisplayActive (bool active) noexcept;
    void setWaveformSeconds (double seconds) noexcept { waveSeconds.store (seconds); }
    void setWaveformTrigger (bool on) noexcept { waveTrigger.store (on); }
    void requestReset() noexcept { resetRequested.store (true); }

    void copySnapshot (EngineSnapshot& out) const;

    // Whole-song history like a loudness meter's timeline: one point per second (aggregated from
    // ten 100 ms points), up to two hours, recorded while the host plays or signal is present.
    static constexpr int longHistoryCapacity = 7200;
    int getLongHistoryVersion() const noexcept { return longVersion.load(); }
    void copyLongHistory (std::vector<History::Point>& out) const;

private:
    void run() override;
    void processChunk (const float* left, const float* right, int n);
    void publish (bool hadOverflow);

    const MonoScoreMeter& mono;
    double rate = 48000.0;
    bool prepared = false;

    juce::AbstractFifo fifo { 1 << 17 };
    juce::AudioBuffer<float> fifoBuffer { 2, 1 << 17 };
    std::atomic<bool> overflowed { false };
    std::atomic<double> latestPpq { 0.0 };
    std::atomic<bool> latestHasPosition { false }, latestPlaying { false };
    std::atomic<int> latestNumerator { 4 }, latestDenominator { 4 };

    std::atomic<int> displayUsers { 0 };
    std::atomic<double> waveSeconds { 0.04 };
    std::atomic<bool> waveTrigger { true };
    std::atomic<bool> resetRequested { false };
    bool displayWasActive = false;

    LevelMeter levels;
    StereoMeter stereo;
    SpectrumAnalyzer spectrum;
    History history;
    std::vector<float> frameLeft, frameRight;
    int frameFill = 0;
    std::vector<float> ringLeft, ringRight;
    int ringPosition = 0;
    int historyCountdown = 0;
    float chunkPeakLeft = 0.0f, chunkPeakRight = 0.0f;
    float pointPeak = 0.0f;   // highest sample since the last history point
    bool chunkClipLeft = false, chunkClipRight = false;
    bool overflowSinceLastPoint = false;

    std::vector<History::Point> longHistory;
    History::Point longAccumulator;
    int longCount = 0, longValid = 0;
    double longCorrelation = 0.0, longScore = 0.0, longRms = 0.0;
    int longScoreCount = 0, longRmsCount = 0;
    mutable juce::SpinLock longLock;
    std::atomic<int> longVersion { 0 };
    void addToLongHistory (const History::Point&, bool recording);

    // Large: kept on the heap so a processor stays small.
    std::unique_ptr<EngineSnapshot> building;
    mutable juce::SpinLock snapshotLock;
    std::unique_ptr<EngineSnapshot> snapshot;
};

} // namespace phaselab
