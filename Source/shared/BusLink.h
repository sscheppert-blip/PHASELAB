#pragma once

#include "core/ABAnalysis.h"
#include "core/Spectrum.h"
#include "transport/Link.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>

namespace phaselab
{

// Validity of the A/B comparison (docs/spec/05). Only "valid" allows time-critical A/B values.
enum class LinkState
{
    disconnected,   // A or B missing
    stale,          // a source stopped delivering (stopped transport, bypass, removed)
    offline,        // the host renders faster than realtime: not verified, comparison locked (D-15)
    noHostTime,     // no timeline position (transport stopped or not provided)
    rateMismatch,
    acquiring,      // waiting for a complete common window
    gap,            // data missing in the newest window
    valid
};

struct SourceInfo
{
    bool connected = false;
    bool stale = false;
    std::string name;
};

struct BusLinkStatus
{
    LinkState state = LinkState::disconnected;
    SourceInfo a, b;
    double sampleRate = 0.0;
    ABResult result;   // only meaningful while state == valid
    int group = 0;

    // Display data of A and B (only while an editor is open, D-29)
    SpectrumResult spectrum;                  // x = A, y = B
    std::array<float, 4096> waveA {}, waveB {};
    int waveLength = 0;                       // newest 40 ms of the joined window
};

// Bus Analyzer side of the link: a worker thread follows slots A and B of the selected group,
// joins them on the timeline and runs the A/B analysis. Nothing here runs on the audio thread.
class BusLink : private juce::Thread
{
public:
    static constexpr double windowSeconds = 0.4;
    static constexpr std::int64_t maxWriteSkewMs = 250;   // A and B of one window must be recent together

    BusLink (std::atomic<float>& groupParameter, std::atomic<float>& cutoffParameter);
    ~BusLink() override;

    void start();
    void stop();

    // One poll + join + analysis; the thread calls it every 50 ms, the tests call it directly.
    void step (std::int64_t now);

    BusLinkStatus getStatus() const;
    void getStatus (BusLinkStatus& out) const;
    void setDisplayActive (bool active) noexcept { displayUsers.fetch_add (active ? 1 : -1); }

private:
    void run() override;

    std::atomic<float>& group;
    std::atomic<float>& cutoff;

    link::SlotTable slots;
    link::SourceTracker trackerA, trackerB;
    ABAnalyzer analyzer;
    std::vector<float> windowA, windowB;
    int currentGroup = -1;
    bool wasValid = false;
    std::int64_t lastAnalysedEnd = 0;
    float lastCutoff = 0.0f;
    ABResult lastResult;
    std::atomic<int> displayUsers { 0 };
    SpectrumAnalyzer spectrum;
    SpectrumResult lastSpectrum;
    std::vector<float> lastWaveA, lastWaveB;
    int lastWaveLength = 0;

    // Large: kept on the heap so a processor stays small.
    std::unique_ptr<BusLinkStatus> work;
    mutable juce::SpinLock statusLock;
    std::unique_ptr<BusLinkStatus> status;
};

} // namespace phaselab
