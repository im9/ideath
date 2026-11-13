#include <ideath/Antialiasing.h>
#include <algorithm>
#include <cmath>

namespace ideath {
namespace detail {

namespace {

double besselI0(double x)
{
    // Power series Σ ((x/2)^k / k!)²; 30 terms reach double precision for
    // the argument range used here (x ≤ β = 8).
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 30; ++k)
    {
        term *= x / (2.0 * k);
        sum += term * term;
    }
    return sum;
}

std::array<float, Halfband2x::kTaps> designTaps()
{
    // Halfband low-pass (cutoff sr_2x / 4): ideal response sin(πk/2)/(πk),
    // k = j − centre, which is 1/2 at k = 0 and 0 at every even k ≠ 0.
    // Kaiser window, β = 8: stop-band ≈ −(β/0.1102 + 8.7) = −81 dB,
    // transition ≈ (A − 8)/(2.285·(N − 1)) = 0.70 rad → 0.11·sr_2x wide.
    constexpr int kN = Halfband2x::kTaps;
    constexpr int kC = kN / 2;
    constexpr double kBeta = 8.0;
    const double pi = 3.14159265358979323846;
    std::array<double, kN> h{};
    double denseSum = 0.0;
    for (int j = 0; j < kN; ++j)
    {
        const int k = j - kC;
        if (k == 0 || k % 2 == 0)
            continue; // centre handled below; even offsets are exact zeros
        const double r = 2.0 * j / (kN - 1) - 1.0;
        const double w = besselI0(kBeta * std::sqrt(1.0 - r * r)) / besselI0(kBeta);
        h[static_cast<size_t>(j)] = std::sin(pi * k / 2.0) / (pi * k) * w;
        denseSum += h[static_cast<size_t>(j)];
    }
    // Unity DC per polyphase branch: dense taps sum to 1/2, centre 1/2.
    std::array<float, kN> out{};
    for (int j = 0; j < kN; ++j)
        out[static_cast<size_t>(j)] = static_cast<float>(h[static_cast<size_t>(j)] * 0.5 / denseSum);
    out[kC] = 0.5f;
    return out;
}

// The 24 dense taps g[i] = h[2i] in a contiguous array for the inner loops.
struct DenseTaps
{
    std::array<float, Halfband2x::kDense> g{};
};

const DenseTaps& denseTaps()
{
    static const DenseTaps d = [] {
        DenseTaps t;
        const auto& h = Halfband2x::taps();
        for (int i = 0; i < Halfband2x::kDense; ++i)
            t.g[static_cast<size_t>(i)] = h[static_cast<size_t>(2 * i)];
        return t;
    }();
    return d;
}

} // namespace

const std::array<float, Halfband2x::kTaps>& Halfband2x::taps()
{
    // Function-local static: computed once, thread-safe, read-only after.
    static const std::array<float, kTaps> h = designTaps();
    return h;
}

Halfband2x::Halfband2x()
    : g_(denseTaps().g.data()) // builds the shared tables off the audio thread
{
}

void Halfband2x::reset()
{
    upHist_.fill(0.0f);
    oddHist_.fill(0.0f);
    evenHist_.fill(0.0f);
    upIdx_ = 0;
    oddIdx_ = 0;
    evenIdx_ = 0;
}

void Halfband2x::upsample(float x, float& even, float& odd)
{
    // Newest-first window: hist[idx + i] = x[n − i], i = 0 … kDense − 1.
    upIdx_ = (upIdx_ == 0 ? kDense : upIdx_) - 1;
    upHist_[static_cast<size_t>(upIdx_)] = x;
    upHist_[static_cast<size_t>(upIdx_ + kDense)] = x;
    const float* w = upHist_.data() + upIdx_;
    const float* g = g_;
    float acc = 0.0f;
    for (int i = 0; i < kDense; ++i)
        acc += g[i] * w[i];
    even = 2.0f * acc;
    odd = w[kCentreDelay]; // 2·h[centre]·x[n − 11] = x[n − 11]
}

float Halfband2x::downsample(float even, float odd)
{
    constexpr int kEvenLen = kCentreDelay + 1;
    oddIdx_ = (oddIdx_ == 0 ? kDense : oddIdx_) - 1;
    oddHist_[static_cast<size_t>(oddIdx_)] = odd;
    oddHist_[static_cast<size_t>(oddIdx_ + kDense)] = odd;
    evenIdx_ = (evenIdx_ == 0 ? kEvenLen : evenIdx_) - 1;
    evenHist_[static_cast<size_t>(evenIdx_)] = even;
    evenHist_[static_cast<size_t>(evenIdx_ + kEvenLen)] = even;

    const float* w = oddHist_.data() + oddIdx_;
    const float* g = g_;
    float acc = 0.0f;
    for (int i = 0; i < kDense; ++i)
        acc += g[i] * w[i];
    return acc + 0.5f * evenHist_[static_cast<size_t>(evenIdx_ + kCentreDelay)];
}

} // namespace detail
} // namespace ideath
