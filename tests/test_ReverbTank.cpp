#include <catch2/catch_test_macros.hpp>
#include <ideath/ReverbTank.h>
#include "ReverbTestUtils.h"
#include <cmath>
#include <vector>

using namespace reverbtest;

static constexpr float kSR = 48000.0f;

// Shared tolerances (derivations)
// -------------------------------
// kT60Tol = 10 %: the reverberation-time JND is ≈ 5 % (ISO 3382-1 Annex A);
//   twice that is the widest error that stays inaudible across a sweep of
//   the decay knob while leaving room for the T30 regression's scatter.
// kLevelTolDb = 1 dB: the level JND for broadband sound is ≈ 1 dB.
static constexpr double kT60Tol = 0.10;
static constexpr double kLevelTolDb = 1.0;

// T60 of the band around `hz`: 0.5 s white-noise burst, band-filtered
// output, ISO 3382 T30.  A noise burst excites every mode in the band.
// (A sine burst excites only the one or two modes within its ≈ 1 Hz
// bandwidth; under line modulation those beat and the T30 regression
// scattered from −23 % to +65 % between neighbouring burst frequencies.)
static double bandT60(float t60, float damp, float hz, float q = 2.0f)
{
    return seedAverage([&](uint32_t seed) {
        ideath::ReverbTank tank;
        tank.prepare(kSR);
        tank.setDecay(t60);
        tank.setDamp(damp);
        const size_t burst = static_cast<size_t>(0.5f * kSR);
        std::vector<float> y(burst + tailSamples(t60, kSR));
        White w;
        w.s = seed;
        for (size_t i = 0; i < y.size(); ++i)
        {
            auto [l, r] = tank.process(i < burst ? 0.5f * w.next() : 0.0f);
            y[i] = 0.5f * (l + r);
        }
        return t30(bandFilter(y, hz, kSR, q), burst, kSR);
    });
}

TEST_CASE("ReverbTank: low-frequency T60 matches setDecay within 10 %", "[reverbtank]")
{
    // Jot absorption gives every line the same dB-per-second loss, so the
    // 250 Hz decay must equal the requested T60 at any line length.
    for (float t60 : { 0.5f, 2.0f, 8.0f, 30.0f })
    {
        const double measured = bandT60(t60, 0.0f, 250.0f);
        INFO("requested " << t60 << " s, measured " << measured << " s");
        REQUIRE(measured > 0.0);
        REQUIRE(std::fabs(measured / t60 - 1.0) <= kT60Tol);
    }
}

TEST_CASE("ReverbTank: damp shortens the 4 kHz decay by the specified absorption",
          "[reverbtank]")
{
    // Spec: T60(4 kHz) = 60 / (60/T60 + kMaxDampDbPerSec · damp).
    // Absorption grows with frequency, so the decay is measured in a
    // 1/6-octave band (q = 8): across a 2/3-octave band it already spans
    // 1.73 s (3.2 kHz) to 1.05 s (5 kHz) at damp 0.5 and the T30 of the
    // mixture leans on the slow lower edge.
    const float t60 = 4.0f;
    for (float damp : { 0.0f, 0.5f, 1.0f })
    {
        const double expect = 60.0 / (60.0 / t60 + ideath::ReverbTank::kMaxDampDbPerSec * damp);
        const double hf = bandT60(t60, damp, 4000.0f, 8.0f);
        INFO("damp " << damp << ": 4 kHz T60 " << hf << " s, expected " << expect << " s");
        REQUIRE(hf > 0.0);
        REQUIRE(std::fabs(hf / expect - 1.0) <= kT60Tol);
    }
}

TEST_CASE("ReverbTank: damp leaves the low-frequency decay alone", "[reverbtank]")
{
    // Damping is high-frequency absorption only: the 250 Hz T60 at damp 1
    // stays within the T60 tolerance of the requested value.
    const double lf = bandT60(4.0f, 1.0f, 250.0f);
    INFO("250 Hz T60 at damp 1: " << lf << " s");
    REQUIRE(std::fabs(lf / 4.0 - 1.0) <= kT60Tol);
}

TEST_CASE("ReverbTank: steady-state RMS gain is 0 dB at kUnityT60", "[reverbtank]")
{
    // The input gain is derived so a stationary broadband input comes out
    // at its own RMS when T60 = kUnityT60.  Energy builds as 1 − e^(−2δt);
    // after 0.6·T60 the residual is 10^(−3.6) → the measurement window
    // starts there.
    ideath::ReverbTank tank;
    tank.prepare(kSR);
    tank.setDecay(ideath::ReverbTank::kUnityT60);
    tank.setDamp(0.0f);

    const size_t settle = static_cast<size_t>(0.6f * ideath::ReverbTank::kUnityT60 * kSR);
    const size_t total = settle + static_cast<size_t>(4.0f * kSR);
    std::vector<float> in(total), l(total), r(total), tap(total);
    White w;
    for (size_t i = 0; i < total; ++i)
    {
        in[i] = 0.5f * w.next();
        auto [a, b] = tank.process(in[i]);
        l[i] = a;
        r[i] = b;
        tap[i] = tank.monoTap();
    }
    const double inRms = rms(in, settle, total);
    const double gL = db(rms(l, settle, total) / inRms);
    const double gR = db(rms(r, settle, total) / inRms);
    const double gTap = db(rms(tap, settle, total) / inRms);
    INFO("L " << gL << " dB, R " << gR << " dB, mono tap " << gTap << " dB");
    REQUIRE(std::fabs(gL) <= kLevelTolDb);
    REQUIRE(std::fabs(gR) <= kLevelTolDb);
    // The mono tap feeds the shimmer voices; its power must equal the
    // output power for the shimmer energy ratio to hold.
    REQUIRE(std::fabs(gTap) <= kLevelTolDb);
}

TEST_CASE("ReverbTank: left and right are decorrelated", "[reverbtank]")
{
    // L and R read orthogonal sign combinations of the lines.  0.3 is the
    // interaural cross-correlation below which concert-hall tails read as
    // fully enveloping (Beranek: 1 − IACC ≥ 0.7 in the best-rated halls).
    ideath::ReverbTank tank;
    tank.prepare(kSR);
    tank.setDecay(3.0f);
    White w;
    double sll = 0, srr = 0, slr = 0;
    for (int i = 0; i < static_cast<int>(6.0f * kSR); ++i)
    {
        auto [l, r] = tank.process(0.5f * w.next());
        if (i < static_cast<int>(2.0f * kSR))
            continue;
        sll += static_cast<double>(l) * l;
        srr += static_cast<double>(r) * r;
        slr += static_cast<double>(l) * r;
    }
    const double corr = slr / std::sqrt(sll * srr);
    INFO("L/R correlation " << corr);
    REQUIRE(std::fabs(corr) <= 0.3);
}

TEST_CASE("ReverbTank: sustained tones stay centred", "[reverbtank]")
{
    // A sustained tone excites only a few modes, and a fixed output tap
    // gives each mode a random L/R projection: low notes leaned up to
    // 15 dB to one side.  Per-note L/R energy difference over 6 s must stay
    // within ±3 dB: an interchannel level difference of 3 dB moves a
    // centred phantom image ≈ 10° — the edge of "still centred" for a
    // drone.  Triangle tones every minor third, C2–C6, at size 0.75.
    const float sr = 44100.0f;
    for (int midi = 36; midi <= 84; midi += 3)
    {
        const double f = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
        ideath::ReverbTank tank;
        tank.prepare(sr);
        tank.setDecay(0.5f * std::pow(60.0f, 0.75f));
        double el = 0.0, er = 0.0;
        for (int i = 0; i < static_cast<int>(8.0f * sr); ++i)
        {
            const float ph = static_cast<float>(std::fmod(f * i / sr, 1.0));
            const float x = 0.3f * (ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph);
            auto [l, r] = tank.process(x);
            if (i >= static_cast<int>(2.0f * sr))
            {
                el += static_cast<double>(l) * l;
                er += static_cast<double>(r) * r;
            }
        }
        const double d = 10.0 * std::log10(er / el);
        INFO("MIDI " << midi << " (" << f << " Hz): R − L = " << d << " dB");
        REQUIRE(std::fabs(d) <= 3.0);
    }
}

TEST_CASE("ReverbTank: freeze holds the tail for 10 s without growth", "[reverbtank]")
{
    // Frozen lines have unit gain and the Householder mix is orthogonal, so
    // the stored energy is conserved.  No 1 s block may exceed the first by
    // more than the 1 dB level JND (growth), nor fall more than 3 dB (half
    // the power — no longer a hold).
    ideath::ReverbTank tank;
    tank.prepare(kSR);
    tank.setDecay(3.0f);
    White w;
    for (int i = 0; i < static_cast<int>(2.0f * kSR); ++i)
        tank.process(0.5f * w.next());
    tank.setFreeze(true);

    const int block = static_cast<int>(kSR);
    double first = 0.0;
    for (int b = 0; b < 10; ++b)
    {
        double e = 0.0;
        for (int i = 0; i < block; ++i)
        {
            auto [l, r] = tank.process(0.0f);
            REQUIRE(std::isfinite(l));
            e += static_cast<double>(l) * l + static_cast<double>(r) * r;
        }
        const double level = std::sqrt(e / block);
        if (b == 0)
            first = level;
        INFO("block " << b << ": " << db(level / first) << " dB vs first");
        REQUIRE(db(level / first) <= 1.0);
        REQUIRE(db(level / first) >= -3.0);
    }
}

TEST_CASE("ReverbTank: reset matches a freshly prepared tank", "[reverbtank]")
{
    ideath::ReverbTank a, b;
    a.prepare(kSR);
    b.prepare(kSR);
    White w;
    for (int i = 0; i < 20000; ++i)
        a.process(w.next());
    a.reset();
    for (int i = 0; i < 20000; ++i)
    {
        const float x = (i == 0) ? 1.0f : 0.0f;
        auto [al, ar] = a.process(x);
        auto [bl, br] = b.process(x);
        REQUIRE(al == bl);
        REQUIRE(ar == br);
    }
}

TEST_CASE("ReverbTank: silent tail never produces subnormals", "[reverbtank]")
{
    // Every feedback state carries the 1e-25 anti-denormal offset; a 10 s
    // silent tail at the shortest decay must never emit a subnormal.
    ideath::ReverbTank tank;
    tank.prepare(kSR);
    tank.setDecay(ideath::ReverbTank::kMinT60);
    tank.setDamp(1.0f);
    tank.process(1.0f);
    for (int i = 0; i < static_cast<int>(10.0f * kSR); ++i)
    {
        auto [l, r] = tank.process(0.0f);
        REQUIRE(std::fpclassify(l) != FP_SUBNORMAL);
        REQUIRE(std::fpclassify(r) != FP_SUBNORMAL);
    }
}
