#include "core/Loudness.h"

#include <algorithm>
#include <cmath>

namespace phaselab
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 30; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    double toDb (double amplitude) { return amplitude > 0.0 ? 20.0 * std::log10 (amplitude) : noLevel; }
}

void Biquad::set (double nb0, double nb1, double nb2, double na1, double na2) noexcept
{
    b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2;
}

void KWeighting::prepare (double sampleRate) noexcept
{
    // Coefficients for any rate, from the analogue prototypes of the 48 kHz filters in BS.1770.
    {
        const double f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan (pi * f0 / sampleRate);
        const double vh = std::pow (10.0, gain / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        shelf.set ((vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                   2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0);
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan (pi * f0 / sampleRate);
        const double a0 = 1.0 + k / q + k * k;
        highPass.set (1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0);
    }
    reset();
}

void KWeighting::reset() noexcept
{
    shelf.reset();
    highPass.reset();
}

TruePeak::TruePeak()
{
    // Windowed-sinc 4x interpolator (Kaiser, beta 8), cut-off at the original Nyquist frequency.
    const int length = phases * tapsPerPhase;
    const double centre = (length - 1) / 2.0;
    const double norm = besselI0 (8.0);
    for (int i = 0; i < length; ++i)
    {
        const double t = (i - centre) / phases;
        const double sinc = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
        const double w = (i - centre) / centre;
        const double kaiser = besselI0 (8.0 * std::sqrt (std::max (0.0, 1.0 - w * w))) / norm;
        coefficients[(size_t) i] = (float) (sinc * kaiser);
    }
    reset();
}

void TruePeak::reset() noexcept
{
    history.fill (0.0f);
    position = 0;
}

float TruePeak::process (float x) noexcept
{
    // Mirrored history so the newest tapsPerPhase samples are always contiguous.
    position = (position + 1) % tapsPerPhase;
    history[(size_t) position] = x;
    history[(size_t) (position + tapsPerPhase)] = x;

    float peak = std::abs (x);
    const float* h = history.data() + position + 1;   // oldest … newest
    for (int p = 0; p < phases; ++p)
    {
        float sum = 0.0f;
        for (int j = 0; j < tapsPerPhase; ++j)
            sum += coefficients[(size_t) (p + phases * (tapsPerPhase - 1 - j))] * h[j];
        peak = std::max (peak, std::abs (sum));
    }
    return peak;
}

LevelMeter::LevelMeter()
    : integratedEnergy (histogramBins, 0.0), rangeEnergy (histogramBins, 0.0),
      integratedCount (histogramBins, 0), rangeCount (histogramBins, 0)
{
}

void LevelMeter::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    blockLength = std::max (1, (int) std::lround (0.1 * rate));
    for (auto& k : kWeighting)
        k.prepare (rate);
    reset();
}

void LevelMeter::reset() noexcept
{
    for (auto& k : kWeighting)
        k.reset();
    for (auto& t : truePeaks)
        t.reset();
    blockWeighted = blockRaw = 0.0;
    blockFill = 0;
    weightedBlocks.fill (0.0);
    rawBlocks.fill (0.0);
    blocksDone = 0;
    peakHold = truePeakHold = 0.0;
    clipsLeft = clipsRight = clippedSamples = 0;
    inClipLeft = inClipRight = false;
    std::fill (integratedEnergy.begin(), integratedEnergy.end(), 0.0);
    std::fill (rangeEnergy.begin(), rangeEnergy.end(), 0.0);
    std::fill (integratedCount.begin(), integratedCount.end(), 0);
    std::fill (rangeCount.begin(), rangeCount.end(), 0);
}

double LevelMeter::loudness (double energy) noexcept
{
    return energy > 0.0 ? -0.691 + 10.0 * std::log10 (energy) : noLevel;
}

int LevelMeter::binOf (double lufs) noexcept
{
    return std::clamp ((int) std::floor ((lufs + 70.0) * 10.0), 0, histogramBins - 1);
}

void LevelMeter::process (const float* left, const float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];

        const bool clipL = std::abs (l) >= 1.0f, clipR = std::abs (r) >= 1.0f;
        if (clipL && ! inClipLeft) ++clipsLeft;
        if (clipR && ! inClipRight) ++clipsRight;
        clippedSamples += (clipL ? 1 : 0) + (clipR ? 1 : 0);
        inClipLeft = clipL;
        inClipRight = clipR;

        peakHold = std::max (peakHold, (double) std::max (std::abs (l), std::abs (r)));
        truePeakHold = std::max (truePeakHold, (double) std::max (truePeaks[0].process (l), truePeaks[1].process (r)));

        const double wl = kWeighting[0].process (l), wr = kWeighting[1].process (r);
        blockWeighted += wl * wl + wr * wr;   // channel weights 1.0 for L and R
        blockRaw += 0.5 * ((double) l * l + (double) r * r);
        if (++blockFill >= blockLength)
            finishBlock();
    }
}

void LevelMeter::finishBlock() noexcept
{
    std::rotate (weightedBlocks.rbegin(), weightedBlocks.rbegin() + 1, weightedBlocks.rend());
    std::rotate (rawBlocks.rbegin(), rawBlocks.rbegin() + 1, rawBlocks.rend());
    weightedBlocks[0] = blockWeighted / blockFill;
    rawBlocks[0] = blockRaw / blockFill;
    blockWeighted = blockRaw = 0.0;
    blockFill = 0;
    ++blocksDone;

    // Gating blocks: 400 ms with 75 % overlap (one per 100 ms).
    if (blocksDone >= 4)
    {
        double energy = 0.0;
        for (int k = 0; k < 4; ++k)
            energy += weightedBlocks[(size_t) k];
        energy /= 4.0;
        const double l = loudness (energy);
        if (l >= -70.0)
        {
            integratedEnergy[(size_t) binOf (l)] += energy;
            ++integratedCount[(size_t) binOf (l)];
        }
    }
    // Short-term values for the loudness range, also every 100 ms.
    if (blocksDone >= 30)
    {
        double energy = 0.0;
        for (double e : weightedBlocks)
            energy += e;
        energy /= 30.0;
        const double l = loudness (energy);
        if (l >= -70.0)
        {
            rangeEnergy[(size_t) binOf (l)] += energy;
            ++rangeCount[(size_t) binOf (l)];
        }
    }
}

LevelValues LevelMeter::values() const noexcept
{
    LevelValues v;
    v.peak = toDb (peakHold);
    v.clipsLeft = clipsLeft;
    v.clipsRight = clipsRight;
    v.clippedSamples = clippedSamples;
    v.truePeak = toDb (truePeakHold);

    const int available = std::min (blocksDone, 30);
    if (available >= 3)
    {
        double raw = 0.0;
        for (double e : rawBlocks)
            raw += e;
        v.rms = raw > 0.0 ? 10.0 * std::log10 (raw / 3.0) : noLevel;
    }
    if (available >= 4)
    {
        double e = 0.0;
        for (int k = 0; k < 4; ++k)
            e += weightedBlocks[(size_t) k];
        v.momentary = std::max (noLevel, loudness (e / 4.0));
    }
    if (available >= 30)
    {
        double e = 0.0;
        for (double b : weightedBlocks)
            e += b;
        v.shortTerm = std::max (noLevel, loudness (e / 30.0));
    }

    // Integrated: mean energy of the blocks above the relative gate (mean of the absolutely gated - 10 LU).
    {
        double energy = 0.0;
        long long count = 0;
        for (int b = 0; b < histogramBins; ++b)
        {
            energy += integratedEnergy[(size_t) b];
            count += integratedCount[(size_t) b];
        }
        if (count > 0)
        {
            const double gate = loudness (energy / (double) count) - 10.0;
            double gated = 0.0;
            long long gatedCount = 0;
            for (int b = 0; b < histogramBins; ++b)
                if (binLufs (b) >= gate)
                {
                    gated += integratedEnergy[(size_t) b];
                    gatedCount += integratedCount[(size_t) b];
                }
            if (gatedCount > 0)
                v.integrated = loudness (gated / (double) gatedCount);
        }
    }

    // Loudness range: 10th to 95th percentile of the short-term values above -20 LU relative.
    {
        double energy = 0.0;
        long long count = 0;
        for (int b = 0; b < histogramBins; ++b)
        {
            energy += rangeEnergy[(size_t) b];
            count += rangeCount[(size_t) b];
        }
        if (count > 0)
        {
            const double gate = loudness (energy / (double) count) - 20.0;
            long long total = 0;
            for (int b = 0; b < histogramBins; ++b)
                if (binLufs (b) >= gate)
                    total += rangeCount[(size_t) b];
            if (total > 0)
            {
                auto percentile = [&] (double p)
                {
                    const long long target = (long long) std::ceil (p * (double) total);
                    long long seen = 0;
                    for (int b = 0; b < histogramBins; ++b)
                        if (binLufs (b) >= gate)
                        {
                            seen += rangeCount[(size_t) b];
                            if (seen >= std::max<long long> (1, target))
                                return binLufs (b);
                        }
                    return binLufs (histogramBins - 1);
                };
                v.range = percentile (0.95) - percentile (0.10);
            }
        }
    }
    return v;
}

} // namespace phaselab
