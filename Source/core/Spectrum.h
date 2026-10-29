#pragma once

// Spectral analysis of a pair of signals (L/R of one track, or A/B in the Bus Analyzer), docs/spec/04:
// smoothed spectra of both signals, of their mid sum and side difference, and per third-octave band
// the correlation (D-24), the phase difference and the energy the mono sum loses (D-25). Also finds
// the most striking narrow resonance of the mid spectrum (R-08).

#include "core/FFT.h"

#include <array>
#include <complex>
#include <vector>

namespace phaselab
{

inline constexpr int spectrumOrder = 12;                 // 4096 points
inline constexpr int spectrumSize = 1 << spectrumOrder;
inline constexpr int displayPoints = 240;                // log-spaced 20 Hz … 20 kHz
inline constexpr int bandCount = 31;                     // third octaves 20 Hz … 20 kHz
inline constexpr double noBand = -1000.0;

double displayFrequency (int point);                     // centre of display point
double bandCentre (int band);

struct SpectrumResult
{
    bool valid = false;
    std::array<float, displayPoints> x {}, y {}, mid {}, side {};   // dB, sine amplitude = level
    std::array<double, bandCount> correlation {};   // -1 … +1, noBand when too quiet
    std::array<double, bandCount> phase {};         // degrees, y relative to x
    std::array<double, bandCount> monoLoss {};      // dB ≤ 0
    double resonanceHz = 0.0;                       // 0 = none
    double resonanceDb = 0.0;                       // above the surrounding spectrum
};

class SpectrumAnalyzer
{
public:
    SpectrumAnalyzer();

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // One analysis frame of spectrumSize samples of each signal. smoothing: weight of the old value
    // (0 = only this frame); a negative value averages all frames since reset with equal weight.
    void addFrame (const float* x, const float* y, double smoothing) noexcept;

    // Averages every frame of a longer stretch (hop = quarter frame); for the Bus Analyzer's window.
    void analyseStretch (const float* x, const float* y, int length) noexcept;

    SpectrumResult result() const noexcept;

private:
    double rate = 48000.0;
    FFT fft { spectrumOrder };
    std::vector<double> window;
    std::vector<std::complex<double>> bufferX, bufferY;
    std::vector<double> powerX, powerY, powerMid, powerSide;
    std::vector<std::complex<double>> cross;
    long long frames = 0;
    double windowGain = 1.0;
};

} // namespace phaselab
