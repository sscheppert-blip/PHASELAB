#pragma once

// Mono score after docs/spec/04 (draft formula, not a standard):
//   EL = mean(L²), ER = mean(R²), EM = mean(((L+R)/2)²)
//   Eref = (sqrt(EL) + sqrt(ER))² / 4        energy of a perfectly coherent sum
//   score = round(100 · clamp(EM / Eref, 0, 1))
// Identical channels → 100, equal opposite polarity → 0, one channel only → 100,
// equal uncorrelated channels → about 50. Silence gives "no value", never 0 or 100.

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace phaselab
{

struct ChannelEnergies
{
    double left = 0.0;
    double right = 0.0;
    double mid = 0.0;      // mean of ((L + R) / 2)²
    std::int64_t samples = 0;

    void add (float l, float r) noexcept
    {
        const double m = 0.5 * (static_cast<double> (l) + static_cast<double> (r));
        left += static_cast<double> (l) * l;
        right += static_cast<double> (r) * r;
        mid += m * m;
        ++samples;
    }

    void reset() noexcept { *this = {}; }
};

// Silence limit for Eref (mean power): -80 dBFS.
inline constexpr double silencePower = 1.0e-8;

// Returns the score 0–100, or a negative value when there is too little signal.
int monoScore (const ChannelEnergies& e) noexcept;

// Fourth-order Linkwitz-Riley low-pass (two cascaded Butterworth biquads), used to limit the
// analysis to the band below the selected cutoff. Only the analysis path is filtered.
class LowPass4
{
public:
    void setup (double sampleRate, double cutoffHz) noexcept;
    void reset() noexcept;
    float process (float x) noexcept;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        float process (float x) noexcept;
    };
    std::array<Biquad, 2> stages;
};

// Accumulates full-band and band-limited energies over fixed windows and publishes the scores for
// other threads. Realtime safe: no allocation, no locks.
class MonoScoreMeter
{
public:
    static constexpr double windowSeconds = 0.4;
    static constexpr double minCutoffHz = 40.0;
    static constexpr double maxCutoffHz = 500.0;

    void prepare (double sampleRate) noexcept;
    void setCutoff (double hz) noexcept;   // call from the audio thread
    void reset() noexcept;
    void process (const float* left, const float* right, int numSamples) noexcept;

    // -1 means "no value" (silence or not measured yet)
    int fullScore() const noexcept { return full.load (std::memory_order_relaxed); }
    int bandScore() const noexcept { return band.load (std::memory_order_relaxed); }
    double cutoff() const noexcept { return cutoffHz; }

private:
    double rate = 48000.0;
    double cutoffHz = 120.0;
    std::int64_t windowLength = 19200;
    ChannelEnergies fullEnergies, bandEnergies;
    LowPass4 lowLeft, lowRight;
    std::atomic<int> full { -1 }, band { -1 };
};

} // namespace phaselab
