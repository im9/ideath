#pragma once

// Measurement helpers shared by the ReverbTank / HallReverb / ShimmerReverb
// tests.  Analysis runs in double; the stimuli are deterministic.

#include <ideath/Biquad.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace reverbtest {

constexpr double kPi = 3.14159265358979323846;

// Deterministic xorshift32 white noise in [−1, 1).
struct White
{
    uint32_t s = 0x12345678u;
    float next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(s) * (2.0f / 4294967296.0f) - 1.0f;
    }
};

// Pink noise: Paul Kellet's economy filter (±0.5 dB of −3 dB/oct from
// 9 Hz to Nyquist at 44.1 kHz), scaled to ≈ white RMS.
struct Pink
{
    White w;
    float b0 = 0, b1 = 0, b2 = 0;
    float next()
    {
        const float x = w.next();
        b0 = 0.99765f * b0 + x * 0.0990460f;
        b1 = 0.96300f * b1 + x * 0.2965164f;
        b2 = 0.57000f * b2 + x * 1.0526913f;
        return 0.25f * (b0 + b1 + b2 + x * 0.1848f);
    }
};

inline double rms(const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0.0;
    for (size_t i = from; i < to; ++i)
        e += static_cast<double>(x[i]) * x[i];
    return std::sqrt(e / static_cast<double>(to - from));
}

inline double db(double ratio) { return 20.0 * std::log10(ratio); }

// Hann-windowed Goertzel power at `hz` over x[from, to).
inline double tonePower(const std::vector<float>& x, size_t from, size_t to,
                        double hz, double sr)
{
    const size_t n = to - from;
    const double w = 2.0 * kPi * hz / sr;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i)
                                                 / static_cast<double>(n - 1));
        const double s0 = win * x[from + i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Sum of tonePower over [centre·2^(−c/1200), centre·2^(c/1200)] at a
// bin spacing of sr / n.
inline double bandPower(const std::vector<float>& x, size_t from, size_t to,
                        double centreHz, double cents, double sr)
{
    const double lo = centreHz * std::pow(2.0, -cents / 1200.0);
    const double hi = centreHz * std::pow(2.0, cents / 1200.0);
    const double step = sr / static_cast<double>(to - from);
    double e = 0.0;
    for (double f = lo; f <= hi; f += step)
        e += tonePower(x, from, to, f, sr);
    return e;
}

// Frequency (in cents relative to `expectHz`) of the strongest component
// within ±rangeCents, scanned on a 1-cent grid.
inline double peakCents(const std::vector<float>& x, size_t from, size_t to,
                        double expectHz, double rangeCents, double sr)
{
    double best = -1.0, bestC = 0.0;
    for (double c = -rangeCents; c <= rangeCents; c += 1.0)
    {
        const double p = tonePower(x, from, to, expectHz * std::pow(2.0, c / 1200.0), sr);
        if (p > best) { best = p; bestC = c; }
    }
    return bestC;
}

// Fourth-order band-pass (two RBJ BPs) around `hz`.  q = 2 → ~2/3-octave
// width; q = 8 → ~1/6 octave.
inline std::vector<float> bandFilter(const std::vector<float>& x, float hz, float sr,
                                     float q = 2.0f)
{
    ideath::Biquad a, b;
    a.setBandpass(hz, q, sr);
    b.setBandpass(hz, q, sr);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = b.process(a.process(x[i]));
    return y;
}

// ISO 3382 T30: Schroeder energy-decay curve of y[from, end), least-squares
// line through −5 … −35 dB, extrapolated to −60 dB.  Returns 0 if the
// decay never reaches −35 dB.
inline double t30(const std::vector<float>& y, size_t from, double sr)
{
    const size_t n = y.size() - from;
    std::vector<double> edc(n);
    double acc = 0.0;
    for (size_t i = n; i-- > 0;)
    {
        acc += static_cast<double>(y[from + i]) * y[from + i];
        edc[i] = acc;
    }
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    bool reached = false;
    for (size_t i = 0; i < n; i += 32)
    {
        const double d = 10.0 * std::log10(edc[i] / edc[0] + 1e-300);
        if (d < -35.0) { reached = true; break; }
        if (d <= -5.0)
        {
            const double t = static_cast<double>(i) / sr;
            sx += t; sy += d; sxx += t * t; sxy += t * d; ++count;
        }
    }
    if (!reached || count < 2)
        return 0.0;
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
    return -60.0 / slope;
}

// Mean of `measure(seed)` over `n` noise seeds.  A single noise-burst T30
// scatters by σ ≈ 5 % at T60 = 0.5 s (16-seed measurement: a 2/3-octave
// band holds only ~60 modes of random amplitude); ISO 3382-1 averages
// several measurements for the same reason.  8 seeds → σ/√8 ≈ 1.8 %.
template <typename Measure>
double seedAverage(Measure&& measure, int n = 8)
{
    double sum = 0.0;
    for (int k = 1; k <= n; ++k)
        sum += measure(0x9E3779B9u * static_cast<uint32_t>(k));
    return sum / n;
}

// Render length that lets a T60 of `t60` reach −35 dB with margin:
// −35 dB is 0.58·T60 after release; 0.8·T60 + 1 s leaves room for the
// pre-delay and the diffuser build-up.
inline size_t tailSamples(double t60, double sr)
{
    return static_cast<size_t>((0.8 * t60 + 1.0) * sr);
}

} // namespace reverbtest
