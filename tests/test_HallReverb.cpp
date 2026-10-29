#include <catch2/catch_test_macros.hpp>
#include <ideath/HallReverb.h>
#include <ideath/ShimmerReverb.h>
#include "ReverbTestUtils.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace reverbtest;

static constexpr float kSR = 48000.0f;

// kT60Tol = 10 %: twice the ≈ 5 % reverberation-time JND (ISO 3382-1).
static constexpr double kT60Tol = 0.10;

// Broadband T30 of the wet mono sum after a 0.5 s pink-noise burst.
template <typename Reverb>
static double broadbandT60(Reverb& rev, float nominalT60)
{
    const size_t burst = static_cast<size_t>(0.5f * kSR);
    std::vector<float> y(burst + tailSamples(nominalT60, kSR));
    Pink p;
    for (size_t i = 0; i < y.size(); ++i)
    {
        auto [l, r] = rev.process(i < burst ? 0.5f * p.next() : 0.0f);
        y[i] = 0.5f * (l + r);
    }
    return t30(y, burst, kSR);
}

static double hallT60(float size, float damp)
{
    ideath::HallReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    rev.setSize(size);
    rev.setDamp(damp);
    return broadbandT60(rev, ideath::HallReverb::sizeToT60(size));
}

TEST_CASE("HallReverb: size map is 0.5 s · 60^size", "[hallreverb]")
{
    REQUIRE(std::fabs(ideath::HallReverb::sizeToT60(0.0f) - 0.5f) < 1e-5f);
    REQUIRE(std::fabs(ideath::HallReverb::sizeToT60(1.0f) - 30.0f) < 1e-3f);
    REQUIRE(std::fabs(ideath::HallReverb::sizeToT60(0.5f)
                      - ideath::ReverbTank::kUnityT60) < 1e-4f);
}

TEST_CASE("HallReverb: T60 grows monotonically with size", "[hallreverb]")
{
    // 0.05 steps across the range at the mid damp setting.  Each step must
    // lengthen the measured broadband T60.
    double prev = 0.0;
    for (int k = 0; k <= 20; ++k)
    {
        const float size = 0.05f * static_cast<float>(k);
        const double t = hallT60(size, 0.5f);
        INFO("size " << size << ": " << t << " s (previous " << prev << " s)");
        REQUIRE(t > prev);
        prev = t;
    }
}

TEST_CASE("HallReverb: low-frequency T60 follows the size map", "[hallreverb]")
{
    for (float size : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        ideath::HallReverb rev;
        rev.prepare(kSR);
        rev.setMix(1.0f);
        rev.setSize(size);
        rev.setDamp(0.5f);
        const double nominal = ideath::HallReverb::sizeToT60(size);
        // Noise burst: excites every mode in the band (see test_ReverbTank
        // for why a sine burst is not a usable T60 stimulus); seed-averaged.
        const double m = seedAverage([&](uint32_t seed) {
            rev.reset();
            const size_t burst = static_cast<size_t>(0.5f * kSR);
            std::vector<float> y(burst + tailSamples(nominal, kSR));
            White w;
            w.s = seed;
            for (size_t i = 0; i < y.size(); ++i)
            {
                auto [l, r] = rev.process(i < burst ? 0.5f * w.next() : 0.0f);
                y[i] = 0.5f * (l + r);
            }
            return t30(bandFilter(y, 250.0f, kSR), burst, kSR);
        });
        INFO("size " << size << ": 250 Hz T60 " << m << " s, nominal " << nominal << " s");
        REQUIRE(std::fabs(m / nominal - 1.0) <= kT60Tol);
    }
}

TEST_CASE("HallReverb: damp takes the highs first", "[hallreverb]")
{
    // Raising damp must shorten the decay above 4 kHz before (more than)
    // the decay below 1 kHz: the 4 kHz-band T60 falls by more than the
    // 250 Hz-band T60, and the 250 Hz T60 stays within tolerance.
    auto bandT60 = [](float damp, float hz) {
        return seedAverage([&](uint32_t seed) {
            ideath::HallReverb rev;
            rev.prepare(kSR);
            rev.setMix(1.0f);
            rev.setSize(0.5f);
            rev.setDamp(damp);
            const size_t burst = static_cast<size_t>(0.5f * kSR);
            std::vector<float> y(burst + tailSamples(ideath::ReverbTank::kUnityT60, kSR));
            White w;
            w.s = seed;
            for (size_t i = 0; i < y.size(); ++i)
            {
                auto [l, r] = rev.process(i < burst ? 0.5f * w.next() : 0.0f);
                y[i] = 0.5f * (l + r);
            }
            return t30(bandFilter(y, hz, kSR), burst, kSR);
        });
    };
    const double hf0 = bandT60(0.0f, 6000.0f), hf1 = bandT60(1.0f, 6000.0f);
    const double lf0 = bandT60(0.0f, 250.0f), lf1 = bandT60(1.0f, 250.0f);
    INFO("6 kHz: " << hf0 << " → " << hf1 << " s;  250 Hz: " << lf0 << " → " << lf1 << " s");
    REQUIRE(hf1 / hf0 < lf1 / lf0);
    REQUIRE(std::fabs(lf1 / lf0 - 1.0) <= kT60Tol);
}

TEST_CASE("HallReverb: pre-delay offsets the tail", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    rev.setPreDelay(0.08f);
    const int pre = static_cast<int>(0.08f * kSR);
    double after = 0.0;
    for (int i = 0; i < pre + static_cast<int>(0.05f * kSR); ++i)
    {
        auto [l, r] = rev.process(i == 0 ? 1.0f : 0.0f);
        if (i < pre)
        {
            REQUIRE(std::fabs(l) < 1e-20f);
            REQUIRE(std::fabs(r) < 1e-20f);
        }
        else
            after += std::fabs(l) + std::fabs(r);
    }
    REQUIRE(after > 0.0);
}

TEST_CASE("HallReverb: pre-delay keeps its time across prepare() at a new rate",
          "[hallreverb]")
{
    // setPreDelay is in seconds; re-preparing at another sample rate must
    // keep 80 ms as 80 ms (first output within one sample of it).
    ideath::HallReverb rev;
    rev.prepare(96000.0f);
    rev.setPreDelay(0.08f);
    rev.setMix(1.0f);
    rev.prepare(44100.0f);
    const int expect = static_cast<int>(0.08f * 44100.0f);
    int first = -1;
    for (int i = 0; i < 2 * expect && first < 0; ++i)
    {
        auto [l, r] = rev.process(i == 0 ? 1.0f : 0.0f);
        if (std::fabs(l) > 1e-12f || std::fabs(r) > 1e-12f)
            first = i;
    }
    INFO("first output at " << first << ", pre-delay " << expect << " samples");
    REQUIRE(first >= expect);
    REQUIRE(first <= expect + static_cast<int>(0.05f * 44100.0f));
}

TEST_CASE("HallReverb: pre-delay clamps at 120 ms", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    rev.setPreDelay(1.0f);
    const int pre = static_cast<int>(ideath::HallReverb::kMaxPreDelay * kSR);
    double after = 0.0;
    for (int i = 0; i < pre + static_cast<int>(0.05f * kSR); ++i)
    {
        auto [l, r] = rev.process(i == 0 ? 1.0f : 0.0f);
        if (i < pre)
            REQUIRE(std::fabs(l) < 1e-20f);
        else
            after += std::fabs(l);
    }
    REQUIRE(after > 0.0);
}

TEST_CASE("HallReverb: modulation changes the tail", "[hallreverb]")
{
    auto render = [](float depth) {
        ideath::HallReverb rev;
        rev.prepare(kSR);
        rev.setMix(1.0f);
        rev.setModDepth(depth);
        std::vector<float> y(48000);
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = rev.process(i == 0 ? 1.0f : 0.0f).first;
        return y;
    };
    auto a = render(0.0f);
    auto b = render(1.0f);
    double diff = 0.0, energy = 0.0;
    for (size_t i = 24000; i < a.size(); ++i)
    {
        diff += (a[i] - b[i]) * (a[i] - b[i]);
        energy += a[i] * a[i];
    }
    // 1 ms excursion at 0.6 Hz decorrelates the late tail; require the
    // difference energy to be at least a tenth of the tail energy
    // (−10 dB: clearly a different waveform, not a rounding difference).
    REQUIRE(diff > 0.1 * energy);
}

TEST_CASE("HallReverb: freeze holds tail and ignores input", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    rev.setSize(0.4f);
    White w;
    for (int i = 0; i < 48000; ++i)
        rev.process(0.5f * w.next());
    rev.setFreeze(true);
    double first = 0.0;
    for (int b = 0; b < 10; ++b)
    {
        double e = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            auto [l, r] = rev.process(w.next()); // full-scale input, muted
            e += static_cast<double>(l) * l + static_cast<double>(r) * r;
        }
        const double level = std::sqrt(e / 48000.0);
        if (b == 0)
            first = level;
        // 1 dB = level JND (no growth); 3 dB = half power (still holding).
        REQUIRE(db(level / first) <= 1.0);
        REQUIRE(db(level / first) >= -3.0);
    }
}

TEST_CASE("HallReverb: input during freeze does not enter the held tail", "[hallreverb]")
{
    auto render = [](bool feed) {
        ideath::HallReverb rev;
        rev.prepare(kSR);
        rev.setMix(1.0f);
        for (int i = 0; i < 4800; ++i)
            rev.process(i == 0 ? 1.0f : 0.0f);
        rev.setFreeze(true);
        std::vector<float> y(20000);
        White w;
        for (auto& s : y)
            s = rev.process(feed ? w.next() : 0.0f).first;
        return y;
    };
    REQUIRE(render(true) == render(false));
}

TEST_CASE("HallReverb: dry/wet mix 0 passes the input bit-exact", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSR);
    rev.setMix(0.0f);
    White w;
    for (int i = 0; i < 10000; ++i)
    {
        const float x = w.next();
        auto [l, r] = rev.process(x);
        REQUIRE(l == x);
        REQUIRE(r == x);
    }
}

TEST_CASE("HallReverb: silence in, silence out; reset returns to silence", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    for (int i = 0; i < 4800; ++i)
        REQUIRE(std::fabs(rev.process(0.0f).first) < 1e-20f);
    White w;
    for (int i = 0; i < 30000; ++i)
        rev.process(w.next());
    rev.reset();
    for (int i = 0; i < 4800; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 1e-20f);
        REQUIRE(std::fabs(r) < 1e-20f);
    }
}

TEST_CASE("HallReverb: extreme settings stay within kMaxOutput", "[hallreverb][stability]")
{
    // Size 1, damp 0, modulation 0 — the sharpest, longest modes.  10 s of
    // full-scale white noise and of full-scale sines.
    const double hz[] = { 0.0, 55.0, 261.63, 1000.0, 3000.0 };
    for (int stim = 0; stim < 5; ++stim)
    {
        ideath::HallReverb rev;
        rev.prepare(kSR);
        rev.setMix(1.0f);
        rev.setSize(1.0f);
        rev.setDamp(0.0f);
        rev.setModDepth(0.0f);
        White w;
        float peak = 0.0f;
        for (size_t i = 0; i < static_cast<size_t>(10.0f * kSR); ++i)
        {
            const float x = stim == 0
                ? w.next()
                : static_cast<float>(std::sin(2.0 * kPi * hz[stim] * static_cast<double>(i) / kSR));
            auto [l, r] = rev.process(x);
            REQUIRE(std::isfinite(l));
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        INFO("stimulus " << stim << " peak " << peak);
        REQUIRE(peak <= ideath::HallReverb::kMaxOutput);
    }
}

TEST_CASE("HallReverb: parameter clamping", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSR);
    rev.setSize(-3.0f);
    rev.setDamp(4.0f);
    rev.setPreDelay(-1.0f);
    rev.setModDepth(-2.0f);
    rev.setModRate(0.0f);
    rev.setMix(9.0f);
    White w;
    for (int i = 0; i < 48000; ++i)
    {
        auto [l, r] = rev.process(w.next());
        REQUIRE(std::isfinite(l));
        REQUIRE(std::fabs(l) <= ideath::HallReverb::kMaxOutput);
    }
}

TEST_CASE("HallReverb: default-constructible", "[hallreverb]")
{
    ideath::HallReverb rev;
    auto [l, r] = rev.process(1.0f);
    REQUIRE(std::isfinite(l));
    REQUIRE(std::isfinite(r));
}

// Hidden: prints the size → broadband T60 table for HallReverb and
// ShimmerReverb (default shimmer settings) at damp 0 / 0.5 / 1.
// Run with:  ./build/ideath_tests "[.t60table]"
TEST_CASE("Reverb size → T60 table", "[.t60table]")
{
    std::printf("| size | nominal | hall d0 | hall d0.5 | hall d1 | shimmer d0 | shimmer d0.5 | shimmer d1 |\n");
    std::printf("|---|---|---|---|---|---|---|---|\n");
    for (int k = 0; k <= 20; ++k)
    {
        const float size = 0.05f * static_cast<float>(k);
        const float nominal = ideath::HallReverb::sizeToT60(size);
        double h[3], s[3];
        const float damps[3] = { 0.0f, 0.5f, 1.0f };
        for (int d = 0; d < 3; ++d)
        {
            h[d] = hallT60(size, damps[d]);
            ideath::ShimmerReverb shim;
            shim.prepare(kSR);
            shim.setMix(1.0f);
            shim.setSize(size);
            shim.setDamp(damps[d]);
            s[d] = broadbandT60(shim, nominal);
        }
        std::printf("| %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f |\n",
                    size, nominal, h[0], h[1], h[2], s[0], s[1], s[2]);
    }
}
