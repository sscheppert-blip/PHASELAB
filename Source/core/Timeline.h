#pragma once

#include <cstdint>
#include <vector>

namespace phaselab
{

// Recent mono audio of one source, addressed by host timeline position (sample index). Every sample
// remembers its position, the block's key (writer generation and epoch) and when it was written, so
// a later read can tell whether a stretch is complete, from one continuous run and fresh.
// Allocates once in the constructor; all other calls are allocation free.
class TimelineBuffer
{
public:
    static constexpr int size = 1 << 17;

    TimelineBuffer();

    void write (std::int64_t position, float value, std::uint64_t key, std::int64_t writtenMs) noexcept;
    void invalidate (std::int64_t position, int count) noexcept;
    void clear() noexcept;

    // Copies [start, start + count) if every sample is present with the given key.
    // Reports when the first and the last sample were written.
    bool copy (std::int64_t start, int count, std::uint64_t key, float* out,
               std::int64_t& firstWrittenMs, std::int64_t& lastWrittenMs) const noexcept;

private:
    static constexpr std::int64_t mask = size - 1;
    std::vector<float> values;
    std::vector<std::int64_t> positions;
    std::vector<std::uint64_t> keys;
    std::vector<std::int64_t> written;
};

} // namespace phaselab
