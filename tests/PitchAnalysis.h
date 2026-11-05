#pragma once

// Spectral pitch / harmonicity measurements shared by the string-model tests
// (KarplusStrong, BowedString).
//
// Method: Hann-windowed radix-2 FFT in double precision (zero-padded when
// the buffer is shorter than the FFT; the tests use equal sizes).
//   - partialPitch(): for each partial n = 1..3, find the magnitude peak
//     within ±50 cents of n·f0, refine it with a parabola through the log
//     magnitudes of the peak bin and its two neighbours (the Hann main lobe
//     is close to Gaussian, so the log-parabola error is < 1 % of a bin),
//     convert (peak / n) to cents relative to f0, and return the median of
//     the three.  ±50 cents is half a semitone: any peak that is still the
//     n-th partial lies inside it, and the search cannot jump to a neighbour.
//   - harmonicity(): every bin above 40 Hz is assigned to the nearest half-
//     integer multiple k·f0/2.  A bin within ±25 cents of an even k counts
//     toward the harmonic series n·f0, within ±25 cents of an odd k toward
//     the half-harmonic series (n+½)·f0, otherwise toward "neither".  Using
//     the nearest grid point keeps the classes disjoint even at high n
//     where the ±25-cent windows would otherwise overlap.

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace pitch_analysis {

inline void fftInPlace(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/// Power spectrum (|X|², bins 0..N/2) of `x` Hann-windowed and zero-padded
/// to `fftSize` (a power of two ≥ x.size()).
inline std::vector<double> powerSpectrum(const std::vector<float>& x, size_t fftSize)
{
    std::vector<std::complex<double>> a(fftSize, 0.0);
    const size_t n = std::min(x.size(), fftSize);
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i)
                                              / static_cast<double>(n - 1));
        a[i] = w * static_cast<double>(x[i]);
    }
    fftInPlace(a);
    std::vector<double> p(fftSize / 2 + 1);
    for (size_t k = 0; k < p.size(); ++k) p[k] = std::norm(a[k]);
    return p;
}

inline double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

/// Peak frequency of the n-th partial (Hz), searched within ±50 cents of n·f0.
inline double partialPeakHz(const std::vector<double>& p, double binHz, double f0, int n)
{
    const double lo = n * f0 * std::pow(2.0, -50.0 / 1200.0);
    const double hi = n * f0 * std::pow(2.0, 50.0 / 1200.0);
    const size_t kLo = std::max<size_t>(1, static_cast<size_t>(std::floor(lo / binHz)));
    const size_t kHi = std::min(p.size() - 2, static_cast<size_t>(std::ceil(hi / binHz)));
    size_t best = kLo;
    for (size_t k = kLo; k <= kHi; ++k)
        if (p[k] > p[best]) best = k;
    const double a = std::log(p[best - 1] + 1e-300);
    const double b = std::log(p[best] + 1e-300);
    const double c = std::log(p[best + 1] + 1e-300);
    const double den = a - 2.0 * b + c;
    const double delta = (den < 0.0) ? 0.5 * (a - c) / den : 0.0;
    return (static_cast<double>(best) + delta) * binHz;
}

struct PitchResult
{
    double partialCents[3];  // (peak_n / n) vs f0, n = 1..3
    double medianCents;
};

inline PitchResult partialPitch(const std::vector<double>& p, double binHz, double f0)
{
    PitchResult r{};
    for (int n = 1; n <= 3; ++n)
        r.partialCents[n - 1] = cents(partialPeakHz(p, binHz, f0, n) / n, f0);
    double s[3] = { r.partialCents[0], r.partialCents[1], r.partialCents[2] };
    std::sort(s, s + 3);
    r.medianCents = s[1];
    return r;
}

struct Harmonicity
{
    double total = 0.0;      // energy above 40 Hz
    double harmonic = 0.0;   // within ±25 cents of n·f0
    double half = 0.0;       // within ±25 cents of (n+½)·f0
    double harmonicFraction() const { return total > 0.0 ? harmonic / total : 0.0; }
    double halfToHarmonic() const { return harmonic > 0.0 ? half / harmonic : 1e9; }
};

inline Harmonicity harmonicity(const std::vector<double>& p, double binHz, double f0)
{
    Harmonicity h;
    for (size_t k = 1; k < p.size(); ++k)
    {
        const double f = static_cast<double>(k) * binHz;
        if (f < 40.0) continue;
        h.total += p[k];
        const double grid = std::max(1.0, std::round(f / (0.5 * f0)));
        if (std::fabs(cents(f, grid * 0.5 * f0)) > 25.0) continue;
        if (static_cast<long long>(grid) % 2 == 0) h.harmonic += p[k];
        else                                       h.half += p[k];
    }
    return h;
}

} // namespace pitch_analysis
