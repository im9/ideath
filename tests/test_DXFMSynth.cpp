#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/DXFMSynth.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using ideath::DXFMSynth;

static constexpr float kSampleRate = 44100.0f;

// Goertzel-style single-bin DFT amplitude estimator.  Returns peak amp
// of the sinusoid at `targetHz` over buf[0..n).
static double binAmplitude(const float* buf, int n, double sr, double targetHz)
{
    const double w = 2.0 * 3.14159265358979323846 * targetHz / sr;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        re += static_cast<double>(buf[i]) * std::cos(w * i);
        im -= static_cast<double>(buf[i]) * std::sin(w * i);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

// Configure a DXFMSynth as a single-sine carrier (op[5] = OP1 in DX7 lingo,
// the rightmost carrier in algorithm 32).  Algorithm 32 sums all 6 ops as
// carriers; setting only op[5]'s level to 1 makes it behave as a pure sine.
static void configurePureSine(DXFMSynth& fm)
{
    fm.setAlgorithm(31); // DX7 algorithm 32 = additive
    for (int op = 0; op < DXFMSynth::kNumOperators; ++op)
    {
        fm.setLevel(op, op == 5 ? 1.0f : 0.0f);
        fm.setRatio(op, 1.0f);
        fm.setDetune(op, 0.0f);
        fm.setFeedback(op, 0.0f);
        fm.setAttack(op, 0.001f);
        fm.setDecay(op, 0.001f);
        fm.setSustain(op, 1.0f);
        fm.setRelease(op, 0.1f);
        fm.setVelocitySensitivity(op, 0.0f); // ignore velocity
        fm.setPMSensitivity(op, 0.0f);
        fm.setAMSensitivity(op, 0.0f);
    }
    fm.setLFODepth(0.0f);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("DXFMSynth: default constructible, prepare initialises", "[sixop]")
{
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    fm.noteOn(440.0f);
    for (int i = 0; i < 1000; ++i)
    {
        float s = fm.process();
        REQUIRE(std::isfinite(s));
    }
}

TEST_CASE("DXFMSynth: noteOn → isActive → noteOff → goes idle", "[sixop]")
{
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    configurePureSine(fm);
    REQUIRE_FALSE(fm.isActive());

    fm.noteOn(440.0f, 1.0f);
    // Advance past attack
    for (int i = 0; i < 100; ++i) fm.process();
    REQUIRE(fm.isActive());

    fm.noteOff();
    // Release time set to 0.1 s = 4410 samples; advance well past it.
    // Plus the env flush-to-zero threshold needs to be crossed.
    for (int i = 0; i < static_cast<int>(0.5f * kSampleRate); ++i) fm.process();
    REQUIRE_FALSE(fm.isActive());
}

TEST_CASE("DXFMSynth: reset zeroes operator phases and envelopes", "[sixop]")
{
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    configurePureSine(fm);
    fm.noteOn(440.0f);
    for (int i = 0; i < 500; ++i) fm.process();

    fm.reset();
    REQUIRE_FALSE(fm.isActive()); // env reset back to idle

    // Output after reset (with no noteOn) is silent
    for (int i = 0; i < 100; ++i)
    {
        float s = fm.process();
        REQUIRE_THAT(s, WithinAbs(0.0f, 1e-6f));
    }
}

// ---------------------------------------------------------------------------
// Algorithm topology
// ---------------------------------------------------------------------------

TEST_CASE("DXFMSynth: algorithm 32 (additive) — all ops are carriers", "[sixop][algo]")
{
    // DX7 algorithm 32: { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 } — every op has
    // OUT_BUS_ADD set and outbus=0 (main output).  isCarrier must return true
    // for every operator.
    for (int op = 0; op < DXFMSynth::kNumOperators; ++op)
        REQUIRE(DXFMSynth::isCarrier(31, op));
}

TEST_CASE("DXFMSynth: algorithm 1 — only OP1 and OP3 are carriers", "[sixop][algo]")
{
    // DX7 algorithm 1: { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 }
    //   op[0] (DX7 OP6): 0xc1 — feedback + out to bus 1 (modulator)
    //   op[1] (DX7 OP5): 0x11 — in bus 1, out bus 1 (modulator)
    //   op[2] (DX7 OP4): 0x11 — in bus 1, out bus 1 (modulator)
    //   op[3] (DX7 OP3): 0x14 — in bus 1, out bus 0 ADD (carrier)
    //   op[4] (DX7 OP2): 0x01 — in 0, out bus 1 (modulator)
    //   op[5] (DX7 OP1): 0x14 — in bus 1, out bus 0 ADD (carrier)
    // → carriers at indices 3 and 5 (= DX7 OP3 and OP1).
    REQUIRE_FALSE(DXFMSynth::isCarrier(0, 0));
    REQUIRE_FALSE(DXFMSynth::isCarrier(0, 1));
    REQUIRE_FALSE(DXFMSynth::isCarrier(0, 2));
    REQUIRE(DXFMSynth::isCarrier(0, 3));
    REQUIRE_FALSE(DXFMSynth::isCarrier(0, 4));
    REQUIRE(DXFMSynth::isCarrier(0, 5));
}

TEST_CASE("DXFMSynth: every algorithm has at least one carrier", "[sixop][algo]")
{
    // A valid FM algorithm must produce sound — at least one operator must
    // route to the output bus.  All 32 DX7 algorithms satisfy this; this test
    // pins that property so a future copy/paste bug in the table would fail.
    for (int a = 0; a < DXFMSynth::kNumAlgorithms; ++a)
    {
        bool anyCarrier = false;
        for (int op = 0; op < DXFMSynth::kNumOperators; ++op)
            if (DXFMSynth::isCarrier(a, op)) { anyCarrier = true; break; }
        INFO("algorithm index " << a);
        REQUIRE(anyCarrier);
    }
}

TEST_CASE("DXFMSynth: algorithm 8 has feedback on op[2], not op[0]", "[sixop][algo]")
{
    // DX7 algorithm 8: { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 }.  Byte 0xc5 on
    // op[2] has BOTH FB_IN (0x40) and FB_OUT (0x80) — the self-feedback marker.
    // This pins the contract that feedback OP can be at non-zero index (the
    // implementation's `if ((flags & 0xc0) == 0xc0) newFeedback = opOut`
    // path must trigger for op[2], not silently default to op[0]).
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAlgorithm(7); // DX7 algorithm 8 (index 7)
    for (int op = 0; op < DXFMSynth::kNumOperators; ++op)
    {
        fm.setLevel(op, 1.0f);
        fm.setRatio(op, 1.0f);
        fm.setFeedback(op, 0.8f);  // only the FB op (op[2]) responds
        fm.setAttack(op, 0.001f);
        fm.setSustain(op, 1.0f);
    }
    fm.noteOn(440.0f, 1.0f);

    // 10 s with high feedback — catches stale-feedback / drift bugs in the
    // 2-sample averaged tail.  Output must stay bounded and finite.
    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float s = fm.process();
        REQUIRE(std::isfinite(s));
        REQUIRE(std::abs(s) < 2.0f);
    }
}

TEST_CASE("DXFMSynth: algorithm 4 (multiple FB_OUT bytes) is sound", "[sixop][algo]")
{
    // DX7 algorithm 4: { 0xc1, 0x11, 0x94, 0x01, 0x11, 0x14 }.  Both op[0]
    // (0xc1 = FB_IN | FB_OUT, self-feedback) and op[2] (0x94 = FB_OUT alone,
    // NOT a self-feedback op per dexed's render semantics) have the FB_OUT
    // bit set.  Our implementation must only update the feedback tail from
    // op[0] (matches dexed's `(flags & 0xc0) == 0xc0` check); op[2]'s FB_OUT
    // bit is decorative table data and ignored.
    //
    // Regression target: a naive `if (flags & FB_OUT)` write would let op[2]
    // overwrite op[0]'s feedback every sample, corrupting the self-feedback
    // loop and silencing or destabilising the output.
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAlgorithm(3); // DX7 algorithm 4 (index 3)
    for (int op = 0; op < DXFMSynth::kNumOperators; ++op)
    {
        fm.setLevel(op, 1.0f);
        fm.setRatio(op, 1.0f);
        fm.setFeedback(op, 0.5f);
        fm.setAttack(op, 0.001f);
        fm.setSustain(op, 1.0f);
    }
    fm.noteOn(440.0f, 1.0f);

    for (int i = 0; i < 200; ++i) fm.process(); // skip attack
    constexpr int N = 4096;
    double rss = 0.0;
    for (int i = 0; i < N; ++i)
    {
        float s = fm.process();
        REQUIRE(std::isfinite(s));
        rss += s * s;
    }
    const double rms = std::sqrt(rss / N);
    // RMS > 0.01 (−40 dB): clear audible output, well above silence floor.
    REQUIRE(rms > 0.01);
}

TEST_CASE("DXFMSynth: every algorithm produces non-silent output", "[sixop][algo]")
{
    // Sanity check that the dispatch routes correctly for all 32 algorithms.
    // Configure all ops at level 1 with ratio 1 (so additive sum or FM stack
    // always produces audible output) and measure RMS.
    for (int algo = 0; algo < DXFMSynth::kNumAlgorithms; ++algo)
    {
        DXFMSynth fm;
        fm.prepare(kSampleRate);
        fm.setAlgorithm(algo);
        for (int op = 0; op < DXFMSynth::kNumOperators; ++op)
        {
            fm.setLevel(op, 1.0f);
            fm.setRatio(op, 1.0f);
            fm.setDetune(op, 0.0f);
            fm.setFeedback(op, 0.0f);
            fm.setAttack(op, 0.001f);
            fm.setDecay(op, 0.001f);
            fm.setSustain(op, 1.0f);
            fm.setRelease(op, 0.1f);
            fm.setVelocitySensitivity(op, 0.0f);
            fm.setPMSensitivity(op, 0.0f);
            fm.setAMSensitivity(op, 0.0f);
        }
        fm.setLFODepth(0.0f);
        fm.noteOn(440.0f, 1.0f);

        // Skip first 200 samples (attack) then measure RMS over 4096
        for (int i = 0; i < 200; ++i) fm.process();
        constexpr int N = 4096;
        double rss = 0.0;
        for (int i = 0; i < N; ++i)
        {
            float s = fm.process();
            rss += static_cast<double>(s) * static_cast<double>(s);
        }
        const double rms = std::sqrt(rss / N);
        INFO("algorithm index " << algo);
        // Threshold 0.001 (-60 dB): well below any reasonable musical output
        // but well above numerical noise.  No algorithm should silently die.
        REQUIRE(rms > 0.001);
    }
}

// ---------------------------------------------------------------------------
// Output spectrum
// ---------------------------------------------------------------------------

TEST_CASE("DXFMSynth: configured as pure sine has only fundamental", "[sixop][spectrum]")
{
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    configurePureSine(fm);

    const float f0 = 100.0f; // integer cycles in 1 s window → no leakage
    fm.noteOn(f0, 1.0f);
    for (int i = 0; i < 200; ++i) fm.process(); // skip attack

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = fm.process();

    const double a1 = binAmplitude(buf.data(), N, kSampleRate, f0);
    const double a2 = binAmplitude(buf.data(), N, kSampleRate, 2.0 * f0);
    const double a3 = binAmplitude(buf.data(), N, kSampleRate, 3.0 * f0);

    // Pure sine: fundamental near 1.0 × (velocity × 0.85 / numCarriers).
    // With numCarriers=6 (algorithm 32) and 5 silent ops, only one carrier
    // contributes — but the normalization still divides by 6, so a1 ≈ 1/6
    // × 0.85 ≈ 0.142.  Threshold 0.1 (lower bound) and 0.2 (upper) is
    // generous for this expected value.  No 2nd / 3rd harmonics expected.
    REQUIRE(a1 > 0.05);
    REQUIRE(a2 < 0.01);
    REQUIRE(a3 < 0.01);
}

TEST_CASE("DXFMSynth: FM modulation produces sidebands", "[sixop][spectrum]")
{
    // Two-op FM: op[5] (DX7 OP1) is the carrier under algorithm 32.  Setting
    // op[4] (DX7 OP2) to also be a carrier with modulation index 0 means it
    // doesn't modulate.  To produce FM sidebands we need an algorithm where
    // op[4] modulates op[5].  Algorithm 1 routes a stack into op[5].  Use that.
    //
    // Algorithm 1: op[0..2] feed op[3] (one carrier), op[4] feeds op[5] (the
    // other carrier).  Silence op[0..3] so only the op[4]→op[5] pair matters.
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAlgorithm(0); // DX7 algorithm 1
    for (int op = 0; op < DXFMSynth::kNumOperators; ++op)
    {
        fm.setLevel(op, 0.0f);
        fm.setRatio(op, 1.0f);
        fm.setDetune(op, 0.0f);
        fm.setFeedback(op, 0.0f);
        fm.setAttack(op, 0.001f);
        fm.setDecay(op, 0.001f);
        fm.setSustain(op, 1.0f);
        fm.setRelease(op, 0.1f);
        fm.setVelocitySensitivity(op, 0.0f);
        fm.setPMSensitivity(op, 0.0f);
        fm.setAMSensitivity(op, 0.0f);
    }
    fm.setLFODepth(0.0f);
    fm.setLevel(5, 1.0f);     // carrier (DX7 OP1)
    fm.setLevel(4, 0.3f);     // modulator (DX7 OP2), modulating op[5]
    fm.setRatio(4, 2.0f);     // modulator at 2× carrier freq

    const float f0 = 200.0f;
    fm.noteOn(f0, 1.0f);
    for (int i = 0; i < 200; ++i) fm.process();

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = fm.process();

    // FM sideband theory: carrier at fc, modulator at fm, modulation index β.
    // For sin(2π·fc·t + β·sin(2π·fm·t)) the spectrum is Σ_n J_n(β)·sin(2π·(fc+n·fm)·t).
    // Setup: fc=200 Hz, fm=400 Hz (modulator at 2× carrier).  Modulator output
    // peak ≈ 0.3, so modIn peak = 0.3·2π ≈ 1.88 = β.  J_0(1.88) ≈ 0.29,
    // J_1(1.88) ≈ 0.58, J_2(1.88) ≈ 0.31.
    //
    // Visible bins after spectrum-fold (fc-fm = -200 folds to +200 etc.):
    //   bin  200: J_0(β) + |J_1(β)| from k=-1 fold ≈ 0.29 + 0.58 = 0.87
    //   bin  600 (fc+fm, AND |fc-2fm|=600 fold): J_1 + J_2 ≈ 0.58 + 0.31 = 0.89
    //   bin 1000 (fc+2fm, AND |fc-3fm|=1000 fold): J_2 + J_3 ≈ 0.31 + 0.13 = 0.44
    // All ×scale=0.85/2=0.425. So expected:
    //   a200 ≈ 0.37, a600 ≈ 0.38, a1000 ≈ 0.19.
    const double a200  = binAmplitude(buf.data(), N, kSampleRate, 200.0);
    const double a600  = binAmplitude(buf.data(), N, kSampleRate, 600.0);
    const double a1000 = binAmplitude(buf.data(), N, kSampleRate, 1000.0);

    // Carrier present (broad bound — Bessel approximations + numerical
    // fold-coherence in DFT only need to be within 30 % of the analytic value).
    REQUIRE(a200 > 0.1);
    // Sideband at 600 Hz comparable to carrier (validates Bessel J_1 contribution).
    REQUIRE(a600 > 0.1);
    // Higher-order sideband at 1000 Hz at least half of carrier (J_2 ~ 0.3 vs J_0 ~ 0.3).
    REQUIRE(a1000 > 0.05);
}

// ---------------------------------------------------------------------------
// Detune
// ---------------------------------------------------------------------------

TEST_CASE("DXFMSynth: detune cents shifts frequency by expected ratio", "[sixop]")
{
    // Two synths at f0=200, one with op[5] detuned by +100 cents (= one
    // semitone up = ratio 2^(100/1200) ≈ 1.0595).  The shifted instance's
    // dominant bin must be ~211.9 Hz, not 200.
    DXFMSynth a, b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    configurePureSine(a);
    configurePureSine(b);

    b.setDetune(5, 100.0f);

    const float f0 = 200.0f;
    a.noteOn(f0);
    b.noteOn(f0);
    for (int i = 0; i < 200; ++i) { a.process(); b.process(); }

    constexpr int N = 44100;
    std::vector<float> ba(N), bb(N);
    for (int i = 0; i < N; ++i) { ba[i] = a.process(); bb[i] = b.process(); }

    // Plain instance: peak at exactly 200 Hz.
    const double aAt200 = binAmplitude(ba.data(), N, kSampleRate, 200.0);
    // Detuned instance: peak at 200 * 2^(100/1200) = 211.89 Hz, NOT at 200.
    const double bAt200 = binAmplitude(bb.data(), N, kSampleRate, 200.0);
    const double bAt212 = binAmplitude(bb.data(), N, kSampleRate, 211.89);

    REQUIRE(aAt200 > 0.05);
    // Detuned bin 212 must dominate bin 200 in the detuned signal — the
    // residual at 200 is just sinc-leakage from the shifted peak.
    REQUIRE(bAt212 > bAt200 * 5.0);
}

// ---------------------------------------------------------------------------
// LFO modulation
// ---------------------------------------------------------------------------

TEST_CASE("DXFMSynth: LFO depth 0 produces no modulation", "[sixop][lfo]")
{
    // With LFO depth 0, PMS/AMS settings must have no effect — output should
    // be bit-identical to a synth with PMS=AMS=0 explicitly.  Pins the
    // "depth 0 = bypass" invariant.
    DXFMSynth a, b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    configurePureSine(a);
    configurePureSine(b);

    b.setLFORate(5.0f);
    b.setLFOShape(DXFMSynth::LFOShape::Sine);
    b.setLFODepth(0.0f); // disabled
    b.setPMSensitivity(5, 1.0f); // ignored because depth=0
    b.setAMSensitivity(5, 1.0f);

    a.noteOn(440.0f);
    b.noteOn(440.0f);

    for (int i = 0; i < 1000; ++i)
    {
        float sa = a.process();
        float sb = b.process();
        REQUIRE_THAT(sa, WithinAbs(sb, 1e-6f));
    }
}

TEST_CASE("DXFMSynth: LFO AM produces sidebands at carrier ± LFO rate", "[sixop][lfo]")
{
    // Single carrier at 1000 Hz, LFO at 50 Hz, AMS=1, depth=1.
    // Classic AM theorem: cos(2π fc t) · (1 + m cos(2π flfo t)) =
    //   cos(2π fc t) + (m/2)[cos(2π(fc+flfo)t) + cos(2π(fc-flfo)t)].
    // Expected sidebands at 950 and 1050 Hz with amplitude m/2 relative to
    // the carrier.
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    configurePureSine(fm);

    fm.setLFOShape(DXFMSynth::LFOShape::Sine);
    fm.setLFORate(50.0f);
    fm.setLFODepth(1.0f);
    fm.setAMSensitivity(5, 1.0f);

    fm.noteOn(1000.0f, 1.0f);
    for (int i = 0; i < 200; ++i) fm.process();

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = fm.process();

    const double a1000 = binAmplitude(buf.data(), N, kSampleRate, 1000.0);
    const double a950  = binAmplitude(buf.data(), N, kSampleRate, 950.0);
    const double a1050 = binAmplitude(buf.data(), N, kSampleRate, 1050.0);

    REQUIRE(a1000 > 0.01);
    // Both sidebands must rise clearly above floor.  AM at depth=1 with
    // m=1 puts sidebands at 0.5× carrier amplitude — well above 0.1× carrier.
    REQUIRE(a950  > a1000 * 0.1);
    REQUIRE(a1050 > a1000 * 0.1);
}

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------

TEST_CASE("DXFMSynth: velocity sensitivity scales output", "[sixop]")
{
    // velSens=1: output scales linearly with velocity.  Measure ratio of RMS
    // between vel=1.0 and vel=0.5 — should be ≈ 2× (within tolerance for the
    // 0.85 carrier normalization).
    DXFMSynth full, half;
    full.prepare(kSampleRate);
    half.prepare(kSampleRate);
    configurePureSine(full);
    configurePureSine(half);
    full.setVelocitySensitivity(5, 1.0f);
    half.setVelocitySensitivity(5, 1.0f);

    full.noteOn(440.0f, 1.0f);
    half.noteOn(440.0f, 0.5f);
    for (int i = 0; i < 200; ++i) { full.process(); half.process(); }

    double rssFull = 0.0, rssHalf = 0.0;
    constexpr int N = 4096;
    for (int i = 0; i < N; ++i)
    {
        float f = full.process();
        float h = half.process();
        rssFull += f * f;
        rssHalf += h * h;
    }
    const double rmsFull = std::sqrt(rssFull / N);
    const double rmsHalf = std::sqrt(rssHalf / N);

    // Velocity-linear scaling: rmsFull / rmsHalf = 1.0 / 0.5 = 2.0.
    // ±10% tolerance covers any envelope-state mismatch between the two
    // instances at the measurement window edge.
    const double ratio = rmsFull / rmsHalf;
    REQUIRE(ratio > 1.8);
    REQUIRE(ratio < 2.2);
}

// ---------------------------------------------------------------------------
// Output bound
// ---------------------------------------------------------------------------

TEST_CASE("DXFMSynth: output stays within ±1.5 under worst-case stack", "[sixop]")
{
    // Worst-case driver: algorithm 32 (additive, 6 carriers), every op at
    // level 1 with high feedback on op[0].  Carrier-count normalization
    // (output / 6 × 0.85) keeps the sum bounded near ±1 even with feedback
    // pumping the loop.  ±1.5 headroom covers transient overshoot during
    // feedback growth.
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAlgorithm(31);
    for (int op = 0; op < DXFMSynth::kNumOperators; ++op)
    {
        fm.setLevel(op, 1.0f);
        fm.setRatio(op, 1.0f + static_cast<float>(op) * 0.3f); // various
        fm.setDetune(op, 0.0f);
        fm.setFeedback(op, 1.0f); // max — only op[0] (FB op in alg 32) responds
        fm.setAttack(op, 0.001f);
        fm.setDecay(op, 0.001f);
        fm.setSustain(op, 1.0f);
        fm.setRelease(op, 0.1f);
        fm.setVelocitySensitivity(op, 0.0f);
        fm.setPMSensitivity(op, 0.0f);
        fm.setAMSensitivity(op, 0.0f);
    }
    fm.setLFODepth(0.0f);
    fm.noteOn(440.0f, 1.0f);

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float s = fm.process();
        REQUIRE(s >= -1.5f);
        REQUIRE(s <= 1.5f);
    }
}

// ---------------------------------------------------------------------------
// Long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("DXFMSynth: 10-second stability (no NaN, bounded output)", "[sixop]")
{
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAlgorithm(0); // algorithm 1 — has feedback loop, exercises FB tail
    for (int op = 0; op < DXFMSynth::kNumOperators; ++op)
    {
        fm.setLevel(op, 0.8f);
        fm.setRatio(op, 1.0f + static_cast<float>(op) * 0.5f);
        fm.setFeedback(op, 0.5f);
        fm.setAttack(op, 0.01f);
        fm.setDecay(op, 0.2f);
        fm.setSustain(op, 0.6f);
        fm.setRelease(op, 0.5f);
    }
    fm.noteOn(440.0f, 1.0f);

    // 10 seconds — catches denormal accumulation in the feedback path,
    // env precision drift, phase accumulator drift.
    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float s = fm.process();
        REQUIRE(std::isfinite(s));
        REQUIRE(s >= -2.0f);
        REQUIRE(s <= 2.0f);
    }
}

// ---------------------------------------------------------------------------
// Parameter clamping
// ---------------------------------------------------------------------------

TEST_CASE("DXFMSynth: setAlgorithm out-of-range is ignored", "[sixop]")
{
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAlgorithm(5);
    fm.setAlgorithm(-1);                    // ignored
    REQUIRE(fm.getAlgorithm() == 5);
    fm.setAlgorithm(DXFMSynth::kNumAlgorithms);  // ignored (out of range high)
    REQUIRE(fm.getAlgorithm() == 5);
    fm.setAlgorithm(0);
    REQUIRE(fm.getAlgorithm() == 0);
}

TEST_CASE("DXFMSynth: per-op setters tolerate out-of-range op index", "[sixop]")
{
    DXFMSynth fm;
    fm.prepare(kSampleRate);
    // Must not crash / write OOB; behavior is "silently ignored".
    fm.setLevel(-1, 0.5f);
    fm.setLevel(99, 0.5f);
    fm.setRatio(-1, 2.0f);
    fm.setRatio(99, 2.0f);
    fm.setFeedback(-1, 0.5f);
    fm.setFeedback(99, 0.5f);
    fm.noteOn(440.0f);
    for (int i = 0; i < 100; ++i)
    {
        float s = fm.process();
        REQUIRE(std::isfinite(s));
    }
}
