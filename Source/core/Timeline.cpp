#include "core/Timeline.h"

#include <algorithm>

namespace phaselab
{

TimelineBuffer::TimelineBuffer()
    : values ((size_t) size, 0.0f),
      positions ((size_t) size, -1),
      keys ((size_t) size, 0),
      written ((size_t) size, 0)
{
}

void TimelineBuffer::write (std::int64_t position, float value, std::uint64_t key, std::int64_t writtenMs) noexcept
{
    if (position < 0)
        return;
    const auto i = (size_t) (position & mask);
    values[i] = value;
    positions[i] = position;
    keys[i] = key;
    written[i] = writtenMs;
}

void TimelineBuffer::invalidate (std::int64_t position, int count) noexcept
{
    for (int k = 0; k < count; ++k)
    {
        const std::int64_t p = position + k;
        if (p < 0)
            continue;
        const auto i = (size_t) (p & mask);
        if (positions[i] == p)
            positions[i] = -1;
    }
}

void TimelineBuffer::clear() noexcept
{
    std::fill (positions.begin(), positions.end(), -1);
}

bool TimelineBuffer::copy (std::int64_t start, int count, std::uint64_t key, float* out,
                           std::int64_t& firstWrittenMs, std::int64_t& lastWrittenMs) const noexcept
{
    if (start < 0 || count <= 0 || count > size)
        return false;

    for (int k = 0; k < count; ++k)
    {
        const std::int64_t p = start + k;
        const auto i = (size_t) (p & mask);
        if (positions[i] != p || keys[i] != key)
            return false;
        out[k] = values[i];
    }

    firstWrittenMs = written[(size_t) (start & mask)];
    lastWrittenMs = written[(size_t) ((start + count - 1) & mask)];
    return true;
}

} // namespace phaselab
