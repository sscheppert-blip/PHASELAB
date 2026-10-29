#include "core/MonoScore.h"

#include <algorithm>

namespace phaselab
{

int monoScore (const ChannelEnergies& e) noexcept
{
    if (e.samples <= 0)
        return -1;

    const double n = static_cast<double> (e.samples);
    const double el = e.left / n, er = e.right / n, em = e.mid / n;
    const double root = std::sqrt (el) + std::sqrt (er);
    const double reference = root * root / 4.0;

    if (reference < silencePower)
        return -1;

    const double q = std::clamp (em / reference, 0.0, 1.0);
    return static_cast<int> (std::lround (100.0 * q));
}

void LowPass4::setup (double sampleRate, double cutoffHz) noexcept
{
    // Bilinear-transformed Butterworth low-pass (Q = 1/sqrt(2)); two of them give Linkwitz-Riley 24 dB.
    const double pi = 3.14159265358979323846;
    const double w0 = 2.0 * pi * std::clamp (cutoffHz, 10.0, 0.45 * sampleRate) / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * 0.70710678118654752);
    const double cosW = std::cos (w0);
    const double a0 = 1.0 + alpha;

    for (auto& s : stages)
    {
        s.b0 = (1.0 - cosW) / 2.0 / a0;
        s.b1 = (1.0 - cosW) / a0;
        s.b2 = s.b0;
        s.a1 = -2.0 * cosW / a0;
        s.a2 = (1.0 - alpha) / a0;
    }
}

void LowPass4::reset() noexcept
{
    for (auto& s : stages)
        s.z1 = s.z2 = 0.0;
}

float LowPass4::Biquad::process (float x) noexcept
{
    // Transposed direct form II
    const double in = x;
    const double out = b0 * in + z1;
    z1 = b1 * in - a1 * out + z2;
    z2 = b2 * in - a2 * out;
    return static_cast<float> (out);
}

float LowPass4::process (float x) noexcept
{
    for (auto& s : stages)
        x = s.process (x);
    return x;
}

void MonoScoreMeter::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    windowLength = std::max<std::int64_t> (1, static_cast<std::int64_t> (windowSeconds * rate));
    lowLeft.setup (rate, cutoffHz);
    lowRight.setup (rate, cutoffHz);
    reset();
}

void MonoScoreMeter::setCutoff (double hz) noexcept
{
    hz = std::clamp (hz, minCutoffHz, maxCutoffHz);
    if (std::abs (hz - cutoffHz) < 1.0e-3)
        return;

    cutoffHz = hz;
    lowLeft.setup (rate, cutoffHz);
    lowRight.setup (rate, cutoffHz);
    // A window measured partly with the old cutoff is not valid for the new one.
    bandEnergies.reset();
}

void MonoScoreMeter::reset() noexcept
{
    fullEnergies.reset();
    bandEnergies.reset();
    lowLeft.reset();
    lowRight.reset();
    full.store (-1, std::memory_order_relaxed);
    band.store (-1, std::memory_order_relaxed);
}

void MonoScoreMeter::process (const float* left, const float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];
        fullEnergies.add (l, r);
        bandEnergies.add (lowLeft.process (l), lowRight.process (r));

        if (fullEnergies.samples >= windowLength)
        {
            full.store (monoScore (fullEnergies), std::memory_order_relaxed);
            fullEnergies.reset();
        }
        if (bandEnergies.samples >= windowLength)
        {
            band.store (monoScore (bandEnergies), std::memory_order_relaxed);
            bandEnergies.reset();
        }
    }
}

} // namespace phaselab
