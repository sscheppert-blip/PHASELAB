#include "core/FFT.h"

#include <cmath>
#include <utility>

namespace phaselab
{

FFT::FFT (int order) : n (1 << order), twiddles ((size_t) (n / 2)), reversed ((size_t) n)
{
    const double pi = 3.14159265358979323846;
    for (int k = 0; k < n / 2; ++k)
        twiddles[(size_t) k] = std::polar (1.0, -2.0 * pi * k / n);

    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if (i & (1 << b))
                r |= 1 << (order - 1 - b);
        reversed[(size_t) i] = r;
    }
}

void FFT::forward (std::complex<double>* data) const noexcept
{
    for (int i = 0; i < n; ++i)
        if (i < reversed[(size_t) i])
            std::swap (data[i], data[reversed[(size_t) i]]);

    for (int length = 2; length <= n; length <<= 1)
    {
        const int half = length / 2;
        const int step = n / length;
        for (int start = 0; start < n; start += length)
            for (int k = 0; k < half; ++k)
            {
                const auto t = twiddles[(size_t) (k * step)] * data[start + k + half];
                data[start + k + half] = data[start + k] - t;
                data[start + k] += t;
            }
    }
}

} // namespace phaselab
