#pragma once

#include <complex>
#include <vector>

namespace phaselab
{

// In-place radix-2 FFT for power-of-two sizes. Allocates in the constructor only.
class FFT
{
public:
    explicit FFT (int order);

    int size() const noexcept { return n; }
    void forward (std::complex<double>* data) const noexcept;

private:
    int n;
    std::vector<std::complex<double>> twiddles;
    std::vector<int> reversed;
};

} // namespace phaselab
