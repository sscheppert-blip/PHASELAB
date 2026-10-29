#pragma once

#include "core/ABAnalysis.h"
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

    mutable juce::SpinLock statusLock;
    BusLinkStatus status;
};

} // namespace phaselab
