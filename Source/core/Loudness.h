#pragma once

// Level and loudness after ITU-R BS.1770 / EBU Tech 3341 and 3342 (docs/spec/04, R-12):
// sample peak, true peak (4x oversampling), RMS (300 ms), momentary (400 ms), short-term (3 s),
// integrated loudness (absolute -70 LUFS and relative -10 LU gate) and loudness range
// (short-term values, absolute -70 and relative -20 LU gate, 10th to 95th percentile).
// The true-peak interpolator is a windowed-sinc design, not the reference filter of the standard;
// its accuracy is part of the measurement contract that is still open (O-07).

#include <array>
#include <vector>

namespace phaselab
{

inline constexpr double noLevel = -1000.0;   // "no value" for every level below

struct LevelValues
{
    double peak = noLevel;        // dBFS, highest since reset
    double truePeak = noLevel;    // dBTP, highest since reset
    double rms = noLevel;         // dBFS, 300 ms
    double momentary = noLevel;   // LUFS
    double shortTerm = noLevel;   // LUFS
    double integrated = noLevel;  // LUFS
    double range = noLevel;       // LU (loudness range)
};

class Biquad
{
public:
    void set (double b0, double b1, double b2, double a1, double a2) noexcept;
    void reset() noexcept { z1 = z2 = 0.0; }
    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// K-weighting: high shelf (head) followed by the RLB high-pass, for any sample rate.
class KWeighting
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    double process (double x) noexcept { return highPass.process (shelf.process (x)); }

private:
    Biquad shelf, highPass;
};

class TruePeak
{
public:
    TruePeak();
    void reset() noexcept;
    // Returns the highest absolute value of the 4x oversampled signal for this sample.
    float process (float x) noexcept;

private:
    static constexpr int phases = 4, tapsPerPhase = 12;
    std::array<float, phases * tapsPerPhase> coefficients {};
    std::array<float, tapsPerPhase * 2> history {};
    int position = 0;
};

class LevelMeter
{
public:
    LevelMeter();

    void prepare (double sampleRate);
    void reset() noexcept;            // clears holds, integrated loudness and range
    void process (const float* left, const float* right, int numSamples) noexcept;

    LevelValues values() const noexcept;

private:
    static constexpr int histogramBins = 750;   // -70 … +5 LUFS in 0.1 LU steps

    void finishBlock() noexcept;
    static double loudness (double energy) noexcept;
    static int binOf (double lufs) noexcept;
    static double binLufs (int bin) noexcept { return -70.0 + 0.1 * (bin + 0.5); }

    double rate = 48000.0;
    int blockLength = 4800;
    std::array<KWeighting, 2> kWeighting;
    std::array<TruePeak, 2> truePeaks;

    double blockWeighted = 0.0, blockRaw = 0.0;
    int blockFill = 0;
    std::array<double, 30> weightedBlocks {};   // mean K-weighted power of the last 30 × 100 ms
    std::array<double, 3> rawBlocks {};
    int blocksDone = 0;

    double peakHold = 0.0, truePeakHold = 0.0;
    std::vector<double> integratedEnergy, rangeEnergy;
    std::vector<long long> integratedCount, rangeCount;
};

} // namespace phaselab
