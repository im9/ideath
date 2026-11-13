#pragma once

// Alias-to-harmonic energy ratio of a periodic tone through a nonlinearity.
//
// Input: a band-limited sawtooth (every harmonic k·f0 below sr/2, amplitude
// 0.95·(2/π)/k, so it carries no alias of its own).  Output spectrum:
// 65 536-point FFT with a 4-term Blackman-Harris window (side lobes −92 dB,
// main lobe ±4 bins — far below the alias levels measured here, so leakage
// from the harmonics cannot be counted as alias).  Every bin within ±6 bins
// of a harmonic k·f0 < sr/2 counts as harmonic energy; every other bin
// above 20 Hz counts as alias.  Aliases that fold exactly onto a harmonic
// (k·f0 − m·sr ≡ 0 mod f0) are counted as harmonic, so the ratio is a lower
// bound on the true alias energy — the same bias for every mode compared.

#include "PitchAnalysis.h"
#include <cmath>
#include <complex>
#include <vector>

namespace alias_metric {

constexpr int kFftSize = 65536;
constexpr int kSkip = 2048; // settle filters / ADAA state before the window

inline std::vector<float> bandlimitedSaw(float f0, float sr, int n)
{
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    const int harmonics = static_cast<int>(0.5f * sr / f0);
    for (int i = 0; i < n; ++i)
    {
        double s = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            s += std::sin(2.0 * M_PI * k * f0 * i / sr) / k;
        x[static_cast<size_t>(i)] = static_cast<float>(s * 2.0 / M_PI * 0.95);
    }
    return x;
}

/// 10·log10(alias energy / harmonic energy) of y[kSkip, kSkip + kFftSize).
inline double aliasDb(const std::vector<float>& y, float f0, float sr)
{
    std::vector<std::complex<double>> a(kFftSize);
    for (int i = 0; i < kFftSize; ++i)
    {
        const double t = 2.0 * M_PI * i / (kFftSize - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t)
                         - 0.01168 * std::cos(3 * t);
        a[static_cast<size_t>(i)] = w * y[static_cast<size_t>(kSkip + i)];
    }
    pitch_analysis::fftInPlace(a);
    const double bin = sr / kFftSize;
    double harmonic = 0.0, alias = 0.0;
    for (int b = 1; b < kFftSize / 2; ++b)
    {
        const double f = b * bin;
        if (f < 20.0)
            continue;
        const double p = std::norm(a[static_cast<size_t>(b)]);
        const double k = std::round(f / f0);
        const bool isHarmonic = k >= 1.0 && k * f0 < 0.5 * sr && std::fabs(f - k * f0) <= 6.0 * bin;
        (isHarmonic ? harmonic : alias) += p;
    }
    return 10.0 * std::log10(alias / harmonic);
}

/// Steady-state gain of a linear(ised) process at `hz`: drive a sine of
/// amplitude `amp` for an integer number of cycles after `settle` samples
/// and correlate with the input frequency (single-bin DFT, exact for an
/// integer cycle count).  Returns |Y| / amp.
template <class Process>
double sineGain(Process&& process, double hz, double sr, double amp, int settle = 4096)
{
    const int cycles = 200;
    const double period = sr / hz;
    const int n = static_cast<int>(std::round(cycles * period));
    const double f = cycles / static_cast<double>(n) * sr; // exact bin
    double re = 0.0, im = 0.0;
    for (int i = 0; i < settle + n; ++i)
    {
        const double ph = 2.0 * M_PI * f * i / sr;
        const float y = process(static_cast<float>(amp * std::sin(ph)));
        if (i >= settle)
        {
            re += y * std::cos(ph);
            im += y * std::sin(ph);
        }
    }
    return 2.0 * std::sqrt(re * re + im * im) / n / amp;
}

} // namespace alias_metric
