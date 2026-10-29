#include "core/Spectrum.h"

#include "core/MonoScore.h"

#include <algorithm>
#include <cmath>

namespace phaselab
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr int bins = spectrumSize / 2 + 1;
}

double displayFrequency (int point)
{
    return 20.0 * std::pow (1000.0, (point + 0.5) / displayPoints);
}

double bandCentre (int band)
{
    return 1000.0 * std::pow (2.0, (band - 17) / 3.0);   // band 17 = 1 kHz, band 0 = 20 Hz
}

SpectrumAnalyzer::SpectrumAnalyzer()
    : window ((size_t) spectrumSize), bufferX ((size_t) spectrumSize), bufferY ((size_t) spectrumSize),
      powerX ((size_t) bins), powerY ((size_t) bins), powerMid ((size_t) bins), powerSide ((size_t) bins),
      cross ((size_t) bins)
{
    double sum = 0.0;
    for (int i = 0; i < spectrumSize; ++i)
    {
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / spectrumSize);   // Hann
        sum += window[(size_t) i];
    }
    windowGain = 2.0 / sum;   // a sine of amplitude A shows as A
}

void SpectrumAnalyzer::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void SpectrumAnalyzer::reset() noexcept
{
    std::fill (powerX.begin(), powerX.end(), 0.0);
    std::fill (powerY.begin(), powerY.end(), 0.0);
    std::fill (powerMid.begin(), powerMid.end(), 0.0);
    std::fill (powerSide.begin(), powerSide.end(), 0.0);
    std::fill (cross.begin(), cross.end(), std::complex<double>());
    frames = 0;
}

void SpectrumAnalyzer::addFrame (const float* x, const float* y, double smoothing) noexcept
{
    for (int i = 0; i < spectrumSize; ++i)
    {
        bufferX[(size_t) i] = { x[i] * window[(size_t) i], 0.0 };
        bufferY[(size_t) i] = { y[i] * window[(size_t) i], 0.0 };
    }
    fft.forward (bufferX.data());
    fft.forward (bufferY.data());

    const double keep = smoothing < 0.0 ? (double) frames / (double) (frames + 1) : (frames == 0 ? 0.0 : smoothing);
    const double take = 1.0 - keep;
    for (int k = 0; k < bins; ++k)
    {
        const auto a = bufferX[(size_t) k] * windowGain;
        const auto b = bufferY[(size_t) k] * windowGain;
        const auto m = 0.5 * (a + b), s = 0.5 * (a - b);
        powerX[(size_t) k] = keep * powerX[(size_t) k] + take * std::norm (a);
        powerY[(size_t) k] = keep * powerY[(size_t) k] + take * std::norm (b);
        powerMid[(size_t) k] = keep * powerMid[(size_t) k] + take * std::norm (m);
        powerSide[(size_t) k] = keep * powerSide[(size_t) k] + take * std::norm (s);
        cross[(size_t) k] = keep * cross[(size_t) k] + take * (a * std::conj (b));
    }
    ++frames;
}

void SpectrumAnalyzer::analyseStretch (const float* x, const float* y, int length) noexcept
{
    reset();
    for (int start = 0; start + spectrumSize <= length; start += spectrumSize / 4)
        addFrame (x + start, y + start, -1.0);
}

SpectrumResult SpectrumAnalyzer::result() const noexcept
{
    SpectrumResult r;
    if (frames == 0)
        return r;
    r.valid = true;

    const double binWidth = rate / spectrumSize;
    auto toDb = [] (double power) { return (float) (power > 1.0e-14 ? 10.0 * std::log10 (power) : -140.0); };

    // Display curve: highest bin power inside each log-spaced point (peaks stay visible), with
    // interpolation where points are narrower than a bin.
    for (int p = 0; p < displayPoints; ++p)
    {
        const double f0 = 20.0 * std::pow (1000.0, (double) p / displayPoints);
        const double f1 = 20.0 * std::pow (1000.0, (double) (p + 1) / displayPoints);
        int k0 = (int) std::floor (f0 / binWidth), k1 = (int) std::ceil (f1 / binWidth);
        k0 = std::clamp (k0, 1, bins - 1);
        k1 = std::clamp (k1, k0, bins - 1);
        double px = 0, py = 0, pm = 0, ps = 0;
        for (int k = k0; k <= k1; ++k)
        {
            px = std::max (px, powerX[(size_t) k]);
            py = std::max (py, powerY[(size_t) k]);
            pm = std::max (pm, powerMid[(size_t) k]);
            ps = std::max (ps, powerSide[(size_t) k]);
        }
        r.x[(size_t) p] = toDb (px);
        r.y[(size_t) p] = toDb (py);
        r.mid[(size_t) p] = toDb (pm);
        r.side[(size_t) p] = toDb (ps);
    }

    // Third-octave bands: correlation, phase and mono loss from band sums.
    for (int b = 0; b < bandCount; ++b)
    {
        const double fc = bandCentre (b);
        const int k0 = std::clamp ((int) std::floor (fc * std::pow (2.0, -1.0 / 6.0) / binWidth), 1, bins - 1);
        const int k1 = std::clamp ((int) std::ceil (fc * std::pow (2.0, 1.0 / 6.0) / binWidth), k0, bins - 1);
        double px = 0, py = 0, pm = 0;
        std::complex<double> c;
        for (int k = k0; k <= k1; ++k)
        {
            px += powerX[(size_t) k];
            py += powerY[(size_t) k];
            pm += powerMid[(size_t) k];
            c += cross[(size_t) k];
        }
        const double quiet = silencePower * (k1 - k0 + 1);
        if (px < quiet || py < quiet || fc > 0.45 * rate)
        {
            r.correlation[(size_t) b] = r.phase[(size_t) b] = r.monoLoss[(size_t) b] = noBand;
            continue;
        }
        r.correlation[(size_t) b] = std::clamp (c.real() / std::sqrt (px * py), -1.0, 1.0);
        r.phase[(size_t) b] = -std::arg (c) * 180.0 / pi;
        const double root = std::sqrt (px) + std::sqrt (py);
        const double reference = root * root / 4.0;
        r.monoLoss[(size_t) b] = std::clamp (10.0 * std::log10 (std::max (pm, 1.0e-20) / reference), -40.0, 0.0);
    }

    // Resonance: the display point that stands out most from its surroundings (±1/3 octave),
    // above 40 Hz and not lost in the noise floor.
    const int span = std::max (1, displayPoints / 30 / 3);
    double best = 0.0;
    int bestPoint = -1;
    for (int p = span; p < displayPoints - span; ++p)
    {
        const double f = displayFrequency (p);
        if (f < 40.0 || f > 0.45 * rate || r.mid[(size_t) p] < -70.0f)
            continue;
        double around = 0.0;
        int n = 0;
        for (int q = p - 3 * span; q <= p + 3 * span; ++q)
            if (q >= 0 && q < displayPoints && std::abs (q - p) > span / 2)
            {
                around += r.mid[(size_t) q];
                ++n;
            }
        const double prominence = r.mid[(size_t) p] - around / std::max (1, n);
        if (prominence > best && r.mid[(size_t) p] >= r.mid[(size_t) (p - 1)] && r.mid[(size_t) p] >= r.mid[(size_t) (p + 1)])
        {
            best = prominence;
            bestPoint = p;
        }
    }
    if (bestPoint >= 0 && best >= 6.0)
    {
        // Refine to the strongest bin of that point.
        const double f0 = 20.0 * std::pow (1000.0, (double) bestPoint / displayPoints);
        const double f1 = 20.0 * std::pow (1000.0, (double) (bestPoint + 1) / displayPoints);
        int k0 = std::clamp ((int) std::floor (f0 / binWidth), 1, bins - 2), k1 = std::clamp ((int) std::ceil (f1 / binWidth), k0, bins - 2);
        int kb = k0;
        for (int k = k0; k <= k1; ++k)
            if (powerMid[(size_t) k] > powerMid[(size_t) kb])
                kb = k;
        const double l = std::log (std::max (powerMid[(size_t) kb - 1], 1e-30)), c = std::log (std::max (powerMid[(size_t) kb], 1e-30)),
                     h = std::log (std::max (powerMid[(size_t) kb + 1], 1e-30));
        const double denom = l - 2.0 * c + h;
        const double shift = std::abs (denom) > 1e-12 ? 0.5 * (l - h) / denom : 0.0;
        r.resonanceHz = (kb + std::clamp (shift, -0.5, 0.5)) * binWidth;
        r.resonanceDb = best;
    }
    return r;
}

} // namespace phaselab
