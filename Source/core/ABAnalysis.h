#pragma once

// A/B comparison of two sources that are already aligned on the host timeline (docs/spec/04, D-23, D-26).
// The time offset found here is the real signal offset between the two measuring points; the join
// never shifts the data to maximise the correlation.

#include <vector>

namespace phaselab
{

struct ABResult
{
    bool enoughSignal = false;   // both sources above the silence limit
    double correlation = 0.0;    // at zero offset, -1 … +1
    int offsetSamples = 0;       // > 0: B is later than A
    double offsetCorrelation = 0.0;
    bool clearOffset = false;    // the correlation peak is strong enough to name an offset
    bool polarityHint = false;   // inverting B would make the peak positive
    int sumScore = -1;           // how well A and B add up below the cutoff, 0–100, -1 = no value
};

class ABAnalyzer
{
public:
    static constexpr double maxOffsetSeconds = 0.010;   // ±10 ms search range
    static constexpr double offsetSeconds = 0.2;        // newest part of the window used for the search
    static constexpr double clearPeak = 0.5;

    // a and b: the same timeline stretch of both sources. Allocates on first use per length.
    ABResult analyse (const float* a, const float* b, int n, double sampleRate, double cutoffHz);

private:
    std::vector<double> prefixB;
    std::vector<float> lowA, lowB;
};

} // namespace phaselab
