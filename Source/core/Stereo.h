#pragma once

// Stereo image of one track or bus (R-09, R-10): L/R correlation, side share and balance, smoothed
// with a 300 ms time constant, and the recent sample pairs for the goniometer.

#include <array>
#include <cstdint>

namespace phaselab
{

struct StereoValues
{
    bool valid = false;          // enough signal
    double correlation = 0.0;    // -1 … +1
    double sidePercent = 0.0;    // share of the side signal in the total energy
    double balanceDb = 0.0;      // > 0: louder on the right
};

class StereoMeter
{
public:
    static constexpr int scopePoints = 1024;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (const float* left, const float* right, int numSamples) noexcept;

    StereoValues values() const noexcept;
    // Newest pairs (mid, side) for the goniometer, oldest first.
    void copyScope (std::array<float, scopePoints>& mid, std::array<float, scopePoints>& side) const noexcept;

private:
    double coefficient = 0.0;
    double ll = 0.0, rr = 0.0, lr = 0.0;
    std::array<float, scopePoints> scopeMid {}, scopeSide {};
    int scopePosition = 0;
    int decimation = 1, decimationCount = 0;
};

// History of one value over the last 90 seconds at 10 points per second (R-13, D-27).
class History
{
public:
    static constexpr int points = 900;

    struct Point
    {
        bool valid = false;
        float value = 0.0f;
        float second = 0.0f;   // e.g. the mono score, 0 … 100, < 0 = none
        double bar = -1.0;     // bar position (1-based, fractional) when the host provides it
        // Levels of the same 100 ms (dB; -1000 = no value)
        float peak = -1000.0f;       // dBFS, highest sample of these 100 ms
        float rms = -1000.0f;        // dBFS, 300 ms
        float momentary = -1000.0f;  // LUFS, 400 ms
        float shortTerm = -1000.0f;  // LUFS, 3 s
        bool clipped = false;        // a sample at or above 0 dBFS in these 100 ms
    };

    void push (const Point& p) noexcept;
    void clear() noexcept;
    // The last `count` points, oldest first.
    void copy (Point* out, int count) const noexcept;

private:
    std::array<Point, points> data {};
    int position = 0;
};

} // namespace phaselab
