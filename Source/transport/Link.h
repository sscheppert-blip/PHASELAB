#pragma once

// Link transport between Channel Analyzers (writers) and the Bus Analyzer (reader), docs/spec/03 and 05.
//
// One shared slot per comparison group and source (A or B). The writer is the audio thread of one
// Channel Analyzer: it stamps every block with the host timeline position and appends it to a ring.
// The reader is a worker thread of the Bus Analyzer: it copies new blocks into its own timeline
// buffer; the join then only uses samples that sit at the same timeline position in A and B.
// Arrival time and callback order never decide what is compared.
//
// Everything in the slot is lock-free and works across plugin binaries and processes. The block
// fields are atomics read seqlock-style (commit counter before and after); the audio ring is checked
// for overwriting after the copy.

#include "core/Timeline.h"
#include "transport/SharedRegion.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace phaselab::link
{

inline constexpr int maxGroups = 4;
inline constexpr int slotsPerGroup = 2;   // A and B (D-04)
inline constexpr std::uint32_t ringFrames = 1u << 17;
inline constexpr std::uint32_t recordCount = 1024;
inline constexpr std::uint32_t maxBlock = 4096;      // longer host blocks are split
inline constexpr std::int64_t staleMs = 500;         // no block for this long: source is stale
inline constexpr std::uint32_t layoutMagic = 0x31424c50;   // "PLB1"
inline constexpr std::uint32_t layoutVersion = 1;

enum RecordFlags : std::uint32_t
{
    hasHostTime = 1u << 0,   // timeline is the host's position while playing
    offlineRender = 1u << 1  // host renders faster than realtime (not verified, D-15)
};

struct Record
{
    std::atomic<std::uint64_t> commit;   // record number + 1 once complete, 0 while written
    std::atomic<std::int64_t> timeline;
    std::atomic<std::uint64_t> framePos;
    std::atomic<std::uint32_t> length;
    std::atomic<std::uint32_t> epoch;
    std::atomic<std::uint32_t> flags;
    std::atomic<std::uint32_t> generation;
    std::atomic<std::int64_t> writtenMs;
};

struct SlotLayout
{
    std::atomic<std::uint32_t> magic;
    std::atomic<std::uint32_t> version;
    std::atomic<std::uint64_t> owner;        // token of the writing Channel Analyzer, 0 = free
    std::atomic<std::int64_t> heartbeatMs;
    std::atomic<std::uint32_t> generation;   // +1 whenever a writer claims the slot
    std::atomic<std::uint32_t> sampleRate;
    std::atomic<std::uint64_t> framesWritten;
    std::atomic<std::uint64_t> recordsWritten;
    std::atomic<std::uint32_t> nameSeq;      // odd while the name is being written
    char name[64];
    Record records[recordCount];
    float frames[ringFrames];                // mono sum (L+R)/2 of the source
};

static_assert (std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<std::int64_t>::is_always_lock_free,
               "the link needs lock-free 64-bit atomics to work across processes");

// Monotonic milliseconds that are comparable between processes on the same machine.
std::int64_t nowMs() noexcept;

// Prefix for the shared memory names; the tests use their own so they never meet a running DAW.
void setNamespace (const std::string& prefix);
std::string slotName (int group, int slot);

// Opens (or creates) the shared slot. Not for the audio thread.
class SlotHandle
{
public:
    bool open (int group, int slot);
    SlotLayout* get() const noexcept { return layout; }

private:
    SharedRegion region;
    SlotLayout* layout = nullptr;
};

// Every slot of every group, opened lazily on a non-realtime thread and kept until destruction,
// so the audio thread can use a pointer without any risk of it going away.
class SlotTable
{
public:
    SlotLayout* acquire (int group, int slot);                    // opens if needed; not realtime
    SlotLayout* lookup (int group, int slot) const noexcept;      // realtime safe

private:
    std::array<std::unique_ptr<SlotHandle>, maxGroups * slotsPerGroup> handles;
    std::array<std::atomic<SlotLayout*>, maxGroups * slotsPerGroup> layouts {};
};

void writeName (SlotLayout&, const std::string& name) noexcept;
std::string readName (const SlotLayout&);

struct BlockTiming
{
    bool hostTime = false;       // host delivers a timeline position and is playing
    std::int64_t timeline = 0;
    double sampleRate = 48000.0;
    bool offline = false;
};

enum class WriteStatus { sent, busy };

// Audio-thread side of one Channel Analyzer.
class Writer
{
public:
    explicit Writer (std::uint64_t token) noexcept : token (token) {}

    WriteStatus write (SlotLayout&, const float* left, const float* right, int numSamples,
                       const BlockTiming&, std::int64_t now) noexcept;
    void release (SlotLayout&) noexcept;
    std::uint64_t getToken() const noexcept { return token; }

private:
    bool claim (SlotLayout&, std::int64_t now) noexcept;
    void append (SlotLayout&, const float* left, const float* right, int n, std::int64_t timeline,
                 std::uint32_t flags, std::int64_t now) noexcept;

    const std::uint64_t token;
    std::uint32_t generation = 0;
    std::uint32_t epoch = 0;
    std::int64_t expectedNext = 0;
    bool continuous = false;
    double lastRate = 0.0;
};

// Worker-thread side: follows one slot and keeps its recent audio by timeline position.
class SourceTracker
{
public:
    struct Status
    {
        bool connected = false;   // a writer owns the slot
        bool stale = false;       // ... but has not delivered for staleMs
        bool hostTime = false;    // latest block has a host timeline position
        bool offline = false;
        double sampleRate = 0.0;
        std::int64_t latestEnd = 0;     // timeline end of the latest block
        std::uint64_t latestKey = 0;    // generation and epoch of the latest block
        std::string name;
    };

    void poll (const SlotLayout&, std::int64_t now);
    void reset();

    const Status& status() const noexcept { return state; }
    const TimelineBuffer& buffer() const noexcept { return timeline; }

private:
    Status state;
    TimelineBuffer timeline;
    std::uint32_t generation = 0;
    std::uint64_t nextRecord = 0;
    bool started = false;
};

} // namespace phaselab::link
