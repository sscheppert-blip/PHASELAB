#include "core/Stereo.h"

#include "core/MonoScore.h"

#include <algorithm>
#include <cmath>

namespace phaselab
{

void StereoMeter::prepare (double sampleRate) noexcept
{
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    coefficient = std::exp (-1.0 / (0.3 * rate));
    decimation = std::max (1, (int) std::lround (rate / 24000.0));
    reset();
}

void StereoMeter::reset() noexcept
{
    ll = rr = lr = 0.0;
    scopeMid.fill (0.0f);
    scopeSide.fill (0.0f);
    scopePosition = 0;
    decimationCount = 0;
}

void StereoMeter::process (const float* left, const float* right, int numSamples) noexcept
{
    const double take = 1.0 - coefficient;
    for (int i = 0; i < numSamples; ++i)
    {
        const double l = left[i], r = right[i];
        ll = coefficient * ll + take * l * l;
        rr = coefficient * rr + take * r * r;
        lr = coefficient * lr + take * l * r;

        if (++decimationCount >= decimation)
        {
            decimationCount = 0;
            scopeMid[(size_t) scopePosition] = (float) (0.5 * (l + r));
            scopeSide[(size_t) scopePosition] = (float) (0.5 * (l - r));
            scopePosition = (scopePosition + 1) % scopePoints;
        }
    }
}

StereoValues StereoMeter::values() const noexcept
{
    StereoValues v;
    if (ll + rr < 2.0 * silencePower)
        return v;
    v.valid = true;
    v.correlation = ll > 0.0 && rr > 0.0 ? std::clamp (lr / std::sqrt (ll * rr), -1.0, 1.0) : 1.0;   // one channel only: mono
    const double mid = 0.25 * (ll + rr + 2.0 * lr), side = 0.25 * (ll + rr - 2.0 * lr);
    v.sidePercent = 100.0 * std::max (0.0, side) / std::max (1.0e-20, mid + side);
    v.balanceDb = std::clamp (10.0 * std::log10 (std::max (rr, 1.0e-20) / std::max (ll, 1.0e-20)), -60.0, 60.0);
    return v;
}

void StereoMeter::copyScope (std::array<float, scopePoints>& mid, std::array<float, scopePoints>& side) const noexcept
{
    for (int i = 0; i < scopePoints; ++i)
    {
        const int j = (scopePosition + i) % scopePoints;
        mid[(size_t) i] = scopeMid[(size_t) j];
        side[(size_t) i] = scopeSide[(size_t) j];
    }
}

void History::push (const Point& p) noexcept
{
    data[(size_t) position] = p;
    position = (position + 1) % points;
}

void History::clear() noexcept
{
    data.fill ({});
    position = 0;
}

void History::copy (Point* out, int count) const noexcept
{
    count = std::clamp (count, 0, points);
    for (int i = 0; i < count; ++i)
        out[i] = data[(size_t) ((position - count + i + points) % points)];
}

} // namespace phaselab
