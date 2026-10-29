#include "transport/Link.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

namespace phaselab::link
{

namespace
{
    std::mutex namespaceLock;
    std::string namespacePrefix = "pl1";

    constexpr std::uint32_t ringMask = ringFrames - 1;
}

std::int64_t nowMs() noexcept
{
    // steady_clock is QueryPerformanceCounter, CLOCK_MONOTONIC or mach_absolute_time: system-wide.
    using namespace std::chrono;
    return duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
}

void setNamespace (const std::string& prefix)
{
    const std::lock_guard<std::mutex> lock (namespaceLock);
    namespacePrefix = "pl1" + prefix;
}

std::string slotName (int group, int slot)
{
    const std::lock_guard<std::mutex> lock (namespaceLock);
    return namespacePrefix + "g" + std::to_string (group + 1) + (slot == 0 ? "A" : "B");
}

//==============================================================================
bool SlotHandle::open (int group, int slot)
{
    if (layout != nullptr)
        return true;
    if (! region.open (slotName (group, slot), sizeof (SlotLayout)))
        return false;

    auto* l = static_cast<SlotLayout*> (region.data());
    std::uint32_t expected = 0;
    if (l->magic.compare_exchange_strong (expected, layoutMagic))
        l->version.store (layoutVersion);
    else if (expected != layoutMagic || l->version.load() != layoutVersion)
    {
        region.close();   // foreign or incompatible memory: never use it
        return false;
    }

    layout = l;
    return true;
}

SlotLayout* SlotTable::acquire (int group, int slot)
{
    if (group < 0 || group >= maxGroups || slot < 0 || slot >= slotsPerGroup)
        return nullptr;
    const auto i = (size_t) (group * slotsPerGroup + slot);
    if (auto* l = layouts[i].load (std::memory_order_acquire))
        return l;

    if (handles[i] == nullptr)
        handles[i] = std::make_unique<SlotHandle>();
    if (! handles[i]->open (group, slot))
        return nullptr;

    layouts[i].store (handles[i]->get(), std::memory_order_release);
    return handles[i]->get();
}

SlotLayout* SlotTable::lookup (int group, int slot) const noexcept
{
    if (group < 0 || group >= maxGroups || slot < 0 || slot >= slotsPerGroup)
        return nullptr;
    return layouts[(size_t) (group * slotsPerGroup + slot)].load (std::memory_order_acquire);
}

//==============================================================================
void writeName (SlotLayout& s, const std::string& name) noexcept
{
    char buffer[sizeof (s.name)] = {};
    std::memcpy (buffer, name.data(), std::min (name.size(), sizeof (buffer) - 1));
    if (std::memcmp (buffer, s.name, sizeof (buffer)) == 0)
        return;

    s.nameSeq.fetch_add (1, std::memory_order_acq_rel);   // odd: being written
    std::atomic_thread_fence (std::memory_order_release);
    std::memcpy (s.name, buffer, sizeof (buffer));
    s.nameSeq.fetch_add (1, std::memory_order_release);
}

std::string readName (const SlotLayout& s)
{
    char buffer[sizeof (s.name)] = {};
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = s.nameSeq.load (std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        std::memcpy (buffer, s.name, sizeof (buffer));
        std::atomic_thread_fence (std::memory_order_acquire);
        if (s.nameSeq.load (std::memory_order_relaxed) == before)
        {
            buffer[sizeof (buffer) - 1] = 0;
            return buffer;
        }
    }
    return {};
}

//==============================================================================
bool Writer::claim (SlotLayout& s, std::int64_t now) noexcept
{
    std::uint64_t current = s.owner.load (std::memory_order_acquire);
    if (current == token)
        return true;

    // Free, or the previous writer stopped delivering: take over. Otherwise the slot stays with the
    // other Channel Analyzer – two sources are never mixed in one slot.
    const bool stale = now - s.heartbeatMs.load (std::memory_order_acquire) > staleMs;
    if (current != 0 && ! stale)
        return false;
    if (! s.owner.compare_exchange_strong (current, token, std::memory_order_acq_rel))
        return false;

    generation = s.generation.fetch_add (1, std::memory_order_acq_rel) + 1;
    epoch = 0;
    continuous = false;
    lastRate = 0.0;
    s.heartbeatMs.store (now, std::memory_order_release);
    return true;
}

WriteStatus Writer::write (SlotLayout& s, const float* left, const float* right, int numSamples,
                           const BlockTiming& timing, std::int64_t now) noexcept
{
    if (! claim (s, now))
        return WriteStatus::busy;

    // A new continuous run starts whenever the timeline jumps (seek, loop, stop/start) or the
    // sample rate changes; samples of different runs are never joined.
    if (timing.sampleRate != lastRate)
    {
        lastRate = timing.sampleRate;
        s.sampleRate.store ((std::uint32_t) (timing.sampleRate + 0.5), std::memory_order_release);
        continuous = false;
    }
    if (! timing.hostTime || ! continuous || timing.timeline != expectedNext)
        ++epoch;
    continuous = timing.hostTime;
    expectedNext = timing.timeline + numSamples;

    std::uint32_t flags = 0;
    if (timing.hostTime)
        flags |= hasHostTime;
    if (timing.offline)
        flags |= offlineRender;

    for (int done = 0; done < numSamples;)
    {
        const int n = std::min<int> ((int) maxBlock, numSamples - done);
        append (s, left + done, right + done, n, timing.timeline + done, flags, now);
        done += n;
    }

    s.heartbeatMs.store (now, std::memory_order_release);
    return WriteStatus::sent;
}

void Writer::append (SlotLayout& s, const float* left, const float* right, int n, std::int64_t timeline,
                     std::uint32_t flags, std::int64_t now) noexcept
{
    const std::uint64_t pos = s.framesWritten.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        s.frames[(pos + (std::uint64_t) i) & ringMask] = 0.5f * (left[i] + right[i]);

    const std::uint64_t number = s.recordsWritten.load (std::memory_order_relaxed);
    Record& r = s.records[number % recordCount];
    r.commit.store (0, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    r.timeline.store (timeline, std::memory_order_relaxed);
    r.framePos.store (pos, std::memory_order_relaxed);
    r.length.store ((std::uint32_t) n, std::memory_order_relaxed);
    r.epoch.store (epoch, std::memory_order_relaxed);
    r.flags.store (flags, std::memory_order_relaxed);
    r.generation.store (generation, std::memory_order_relaxed);
    r.writtenMs.store (now, std::memory_order_relaxed);
    r.commit.store (number + 1, std::memory_order_release);

    s.framesWritten.store (pos + (std::uint64_t) n, std::memory_order_release);
    s.recordsWritten.store (number + 1, std::memory_order_release);
}

void Writer::release (SlotLayout& s) noexcept
{
    std::uint64_t mine = token;
    s.owner.compare_exchange_strong (mine, 0, std::memory_order_acq_rel);
}

//==============================================================================
void SourceTracker::reset()
{
    state = {};
    timeline.clear();
    started = false;
}

void SourceTracker::poll (const SlotLayout& s, std::int64_t now)
{
    const std::uint64_t owner = s.owner.load (std::memory_order_acquire);
    state.connected = owner != 0;
    state.stale = state.connected && now - s.heartbeatMs.load (std::memory_order_acquire) > staleMs;
    state.sampleRate = (double) s.sampleRate.load (std::memory_order_acquire);
    state.name = readName (s);

    const std::uint32_t gen = s.generation.load (std::memory_order_acquire);
    const std::uint64_t total = s.recordsWritten.load (std::memory_order_acquire);

    if (! started || gen != generation)
    {
        // A different writer took the slot: nothing of the old one may be joined with it.
        timeline.clear();
        generation = gen;
        nextRecord = total > recordCount ? total - recordCount : 0;
        started = true;
    }
    if (total - nextRecord > recordCount)
        nextRecord = total - recordCount;   // fell behind: the oldest blocks are gone

    for (std::uint64_t number = nextRecord; number < total; ++number)
    {
        const Record& r = s.records[number % recordCount];
        const std::uint64_t commit = r.commit.load (std::memory_order_acquire);
        if (commit != number + 1)
            continue;

        const std::int64_t start = r.timeline.load (std::memory_order_relaxed);
        const std::uint64_t pos = r.framePos.load (std::memory_order_relaxed);
        const auto length = (int) std::min<std::uint32_t> (r.length.load (std::memory_order_relaxed), maxBlock);
        const std::uint32_t ep = r.epoch.load (std::memory_order_relaxed);
        const std::uint32_t flags = r.flags.load (std::memory_order_relaxed);
        const std::uint32_t recGen = r.generation.load (std::memory_order_relaxed);
        const std::int64_t written = r.writtenMs.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (r.commit.load (std::memory_order_relaxed) != commit || recGen != generation)
            continue;

        const std::uint64_t key = ((std::uint64_t) recGen << 32) | ep;
        state.hostTime = (flags & hasHostTime) != 0;
        state.offline = (flags & offlineRender) != 0;
        if (! state.hostTime)
            continue;

        for (int i = 0; i < length; ++i)
            timeline.write (start + i, s.frames[(pos + (std::uint64_t) i) & ringMask], key, written);

        // The ring may have wrapped while copying: then this stretch is not trustworthy.
        std::atomic_thread_fence (std::memory_order_acquire);
        if (s.framesWritten.load (std::memory_order_acquire) - pos > ringFrames)
        {
            timeline.invalidate (start, length);
            continue;
        }

        state.latestEnd = start + length;
        state.latestKey = key;
    }

    nextRecord = total;
}

} // namespace phaselab::link
