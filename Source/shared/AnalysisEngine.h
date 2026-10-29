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

inline constexpr int waveformCapacity = 4096;

// What the editor shows of the plugin's own input. Copied as a whole under a spin lock.
struct EngineSnapshot
{
    LevelValues levels;
    StereoValues stereo;
    SpectrumResult spectrum;
    std::array<float, StereoMeter::scopePoints> scopeMid {}, scopeSide {};
    std::array<float, waveformCapacity> waveLeft {}, waveRight {};
    int waveLength = 0;
    float meterLeft = 0.0f, meterRight = 0.0f;   // peak of the newest chunk
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
    void requestReset() noexcept { resetRequested.store (true); }

    void copySnapshot (EngineSnapshot& out) const;

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
    bool overflowSinceLastPoint = false;

    // Large: kept on the heap so a processor stays small.
    std::unique_ptr<EngineSnapshot> building;
    mutable juce::SpinLock snapshotLock;
    std::unique_ptr<EngineSnapshot> snapshot;
};

} // namespace phaselab
