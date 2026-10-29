#include "core/ABAnalysis.h"

#include "core/MonoScore.h"

#include <algorithm>
#include <cmath>

namespace phaselab
{

ABResult ABAnalyzer::analyse (const float* a, const float* b, int n, double sampleRate, double cutoffHz)
{
    ABResult result;
    if (n <= 0 || sampleRate <= 0.0)
        return result;

    double ea = 0.0, eb = 0.0, eab = 0.0;
    for (int i = 0; i < n; ++i)
    {
        ea += (double) a[i] * a[i];
        eb += (double) b[i] * b[i];
        eab += (double) a[i] * b[i];
    }
    if (ea / n < silencePower || eb / n < silencePower)
        return result;

    result.enoughSignal = true;
    result.correlation = eab / std::sqrt (ea * eb);

    // Offset: normalised cross-correlation r(k) = Σ a[i]·b[i+k] over the newest part of the window.
    const int maxLag = std::max (1, (int) std::lround (maxOffsetSeconds * sampleRate));
    const int span = std::min (n - 2 * maxLag, (int) std::lround (offsetSeconds * sampleRate));
    if (span > maxLag)
    {
        const int first = n - maxLag - span;   // a[first … first+span), b shifted by -maxLag … +maxLag

        prefixB.assign ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i)
            prefixB[(size_t) i + 1] = prefixB[(size_t) i] + (double) b[i] * b[i];

        double energyA = 0.0;
        for (int i = first; i < first + span; ++i)
            energyA += (double) a[i] * a[i];

        double best = 0.0;
        int bestLag = 0;
        for (int k = -maxLag; k <= maxLag; ++k)
        {
            double sum = 0.0;
            const float* bk = b + first + k;
            const float* ak = a + first;
            for (int i = 0; i < span; ++i)
                sum += (double) ak[i] * bk[i];

            const double energyB = prefixB[(size_t) (first + k + span)] - prefixB[(size_t) (first + k)];
            const double r = energyA > 0.0 && energyB > 0.0 ? sum / std::sqrt (energyA * energyB) : 0.0;
            if (std::abs (r) > std::abs (best) + 1.0e-12)
            {
                best = r;
                bestLag = k;
            }
        }

        result.offsetSamples = bestLag;
        result.offsetCorrelation = best;
        result.clearOffset = std::abs (best) >= clearPeak;
        result.polarityHint = result.clearOffset && best < 0.0;
    }

    // Sum score A+B below the cutoff: the mono score formula with A as "left" and B as "right".
    lowA.resize ((size_t) n);
    lowB.resize ((size_t) n);
    LowPass4 filterA, filterB;
    filterA.setup (sampleRate, cutoffHz);
    filterB.setup (sampleRate, cutoffHz);
    for (int i = 0; i < n; ++i)
    {
        lowA[(size_t) i] = filterA.process (a[i]);
        lowB[(size_t) i] = filterB.process (b[i]);
    }

    ChannelEnergies sum;
    const int warmUp = std::min (n / 4, (int) (0.05 * sampleRate));   // let the filters settle
    for (int i = warmUp; i < n; ++i)
        sum.add (lowA[(size_t) i], lowB[(size_t) i]);
    result.sumScore = monoScore (sum);

    return result;
}

} // namespace phaselab
