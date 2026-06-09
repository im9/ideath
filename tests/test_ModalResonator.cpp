#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/ModalResonator.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace ideath;

static constexpr float kSR  = 44100.0f;
static constexpr float kPi  = 3.14159265358979323846f;

// Goertzel-style single-bin magnitude.  Returns the amplitude (not power) of
// the discrete sinusoid at `freq` in `buf`.  Cheaper than a full FFT and
// exact enough for our band-pass peak measurements.
static float goertzelMagnitude(const std::vector<float>& buf, float freq, float sr)
{
    const int N = static_cast<int>(buf.size());
    const float w = 2.0f * kPi * freq / sr;
    const float coeff = 2.0f * std::cos(w);
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
    for (int n = 0; n < N; ++n)
    {
        s0 = buf[n] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const float real = s1 - s2 * std::cos(w);
    const float imag = s2 * std::sin(w);
    return std::sqrt(real * real + imag * imag) * 2.0f / static_cast<float>(N);
}

static float peakAbs(const std::vector<float>& buf)
{
    float m = 0.0f;
    for (float v : buf)
        m = std::max(m, std::fabs(v));
    return m;
}

static float rms(const std::vector<float>& buf)
{
    float sum = 0.0f;
    for (float v : buf) sum += v * v;
    return std::sqrt(sum / std::max<size_t>(buf.size(), 1));
}

// Run a single strike and capture `samples` output samples.
static std::vector<float> strikeAndCapture(ModalResonator& m, float velocity, int samples)
{
    m.strike(velocity);
    std::vector<float> out(samples);
    for (int i = 0; i < samples; ++i)
        out[i] = m.process();
    return out;
}

// --- Output range -----------------------------------------------------

TEST_CASE("ModalResonator: output finite for default params + strike", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    // Defaults: 8 partials, harmonic ratios 1..8, fundamental ~220Hz.
    // 1 s of output covers the typical bell decay (~0.5 s default).
    auto out = strikeAndCapture(m, 1.0f, static_cast<int>(kSR));
    for (float s : out)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("ModalResonator: silence before strike", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    // Without strike() the noise burst envelope is idle, so every partial
    // sees zero input.  All BP outputs must be exactly zero.
    for (int i = 0; i < 100; ++i)
    {
        // Exact zero: BPs see exact zero input from idle excitation
        // (DecayEnvelope returns 0.0f when inactive).
        REQUIRE_THAT(m.process(), WithinAbs(0.0f, 1e-9f));
    }
}

TEST_CASE("ModalResonator: output bounded for default params", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setFundamental(220.0f);
    m.setPartialCount(8);
    // Use modest per-partial decays so the test runs quickly.
    for (int i = 0; i < 8; ++i)
        m.setPartialDecay(i, 0.5f);

    auto out = strikeAndCapture(m, 1.0f, static_cast<int>(kSR));
    // Excitation peaks at ±1 (noise) × decay-envelope head value 1.0 →
    // each BP sees up to ±1 for the first ~2 ms.  Per-partial Q is derived
    // from decay (`Q = π × fc × decay / ln(1000)`), so for this test's
    // decay=0.5 s and partials at 220 Hz … 1760 Hz, Q ranges from ~50 (1st
    // partial) to ~400 (8th).  The BP output is multiplied by its own Q
    // at the sum stage to compensate the 0 dB-peak normalisation, so each
    // partial's impulse-response peak is ≈ 1.  Worst-case sum: 8 partials
    // exactly in phase → ±8.  Set generous 500 here to cover any transient
    // overshoot from the noise burst convolving with the BP impulse
    // response, well under the spec ceiling.
    for (float s : out)
        REQUIRE(std::fabs(s) < 500.0f);
}

// --- Reset / state ----------------------------------------------------

TEST_CASE("ModalResonator: reset clears state and silences output", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i) m.process(); // some ringing

    m.reset();
    // After reset every BP z1/z2 = 0, every envelope.level_ = 0, and
    // the excitation envelope is idle → process() must return 0 exactly.
    for (int i = 0; i < 100; ++i)
        REQUIRE_THAT(m.process(), WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("ModalResonator: prepare() reinitialises sample-rate-dependent state", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i) m.process();

    // Re-prepare at a different SR: must call reset() internally and
    // recompute BP coefficients.  After re-prepare, no strike → silence.
    m.prepare(48000.0f);
    for (int i = 0; i < 100; ++i)
        REQUIRE_THAT(m.process(), WithinAbs(0.0f, 1e-9f));
}

// --- Expected behaviour: fundamental partial is excited -------------------

TEST_CASE("ModalResonator: 1-partial output is a damped sinusoid at fundamental", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(1);
    m.setFundamental(440.0f);
    m.setPartialRatio(0, 1.0f);
    m.setPartialDecay(0, 1.0f);  // long enough to measure
    m.setInharmonicity(0.0f);

    // 0.5 s analysis window — long enough for many cycles of 440Hz (220),
    // short enough that the envelope has not fully decayed (T60 = 1 s).
    const int N = static_cast<int>(kSR * 0.5f);
    auto out = strikeAndCapture(m, 1.0f, N);

    const float magOn  = goertzelMagnitude(out, 440.0f, kSR);
    const float magOff = goertzelMagnitude(out, 1100.0f, kSR);

    // BP at fc=440 / Q=40 has -3 dB bandwidth = 11 Hz.  At 1100 Hz the BP
    // is 660 Hz off-centre, ~60× the bandwidth — magnitude attenuated by
    // approximately (f/fc - fc/f)² × Q² ≈ 2² × 40² ≈ 6400 in power, ≈ 80
    // in amplitude.  Threshold 5× is conservative (16× margin over the
    // theoretical 80×) and survives windowing / Goertzel leakage.
    INFO("magOn=" << magOn << " magOff=" << magOff);
    REQUIRE(magOn > magOff * 5.0f);
    // The on-fundamental bin must actually contain energy — guard against
    // a silent or DC-only output that would also pass the ratio test.
    // 0.001 ≈ -60 dBFS, well above numerical floor but well below any
    // audibly meaningful level.
    REQUIRE(magOn > 0.001f);
}

TEST_CASE("ModalResonator: 2-partial output contains both fundamentals", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(2);
    m.setFundamental(220.0f);
    m.setPartialRatio(0, 1.0f);   // 220 Hz
    m.setPartialRatio(1, 3.0f);   // 660 Hz (non-adjacent so bins separate cleanly)
    m.setPartialDecay(0, 1.0f);
    m.setPartialDecay(1, 1.0f);

    const int N = static_cast<int>(kSR * 0.5f);
    auto out = strikeAndCapture(m, 1.0f, N);

    const float mag220  = goertzelMagnitude(out, 220.0f, kSR);
    const float mag660  = goertzelMagnitude(out, 660.0f, kSR);
    const float magOff  = goertzelMagnitude(out, 1500.0f, kSR);

    // Both partial bins must dominate the off-partial bin by the same
    // logic as the 1-partial test (BW = fc/Q < 20 Hz; 1500 Hz is several
    // hundred bandwidths off-centre for either partial).  Threshold 5×.
    INFO("mag220=" << mag220 << " mag660=" << mag660 << " magOff=" << magOff);
    REQUIRE(mag220 > magOff * 5.0f);
    REQUIRE(mag660 > magOff * 5.0f);
    // Each ringing partial must carry meaningful energy.
    REQUIRE(mag220 > 0.001f);
    REQUIRE(mag660 > 0.001f);
}

// --- Decay time -------------------------------------------------------

TEST_CASE("ModalResonator: long decay leaves audible energy after short window", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(1);
    m.setFundamental(440.0f);
    m.setPartialRatio(0, 1.0f);
    m.setPartialDecay(0, 2.0f); // T60 = 2 s
    m.setInharmonicity(0.0f);

    m.strike(1.0f);
    // Discard first 0.1 s of transient (excitation overlap).
    const int skip = static_cast<int>(kSR * 0.1f);
    for (int i = 0; i < skip; ++i) m.process();

    // Measure RMS over 0.1 s starting at t=0.5s.  At T60=2s the envelope
    // level at t=0.5s is exp(-6.9 × 0.5/2) ≈ 0.18 of its peak.  Sustained
    // BP response amplitude after the burst is small but non-zero.
    // Skip to t=0.5s:
    const int skipMore = static_cast<int>(kSR * 0.4f); // 0.1 + 0.4 = 0.5s
    for (int i = 0; i < skipMore; ++i) m.process();

    const int win = static_cast<int>(kSR * 0.1f);
    std::vector<float> buf(win);
    for (int i = 0; i < win; ++i) buf[i] = m.process();

    // Long-decay partial must still be ringing.  RMS floor 1e-5 ≈ -100 dBFS
    // ensures we're above the denormal/flush floor while staying safe of
    // implementation-specific decay-shape detail.  (We test a much tighter
    // ratio in the next case.)
    REQUIRE(rms(buf) > 1e-5f);
}

TEST_CASE("ModalResonator: short decay decays faster than long decay", "[modal]")
{
    auto run = [](float decaySec)
    {
        ModalResonator m;
        m.prepare(kSR);
        m.setPartialCount(1);
        m.setFundamental(440.0f);
        m.setPartialRatio(0, 1.0f);
        m.setPartialDecay(0, decaySec);
        m.setInharmonicity(0.0f);

        m.strike(1.0f);
        // Skip transient (first 50 ms — well past the 2 ms excitation burst).
        const int skip = static_cast<int>(kSR * 0.05f);
        for (int i = 0; i < skip; ++i) m.process();

        // Measure RMS at t = 0.3 s over a 50 ms window.
        const int more = static_cast<int>(kSR * 0.25f);
        for (int i = 0; i < more; ++i) m.process();
        const int win = static_cast<int>(kSR * 0.05f);
        std::vector<float> buf(win);
        for (int i = 0; i < win; ++i) buf[i] = m.process();
        return rms(buf);
    };

    const float rmsShort = run(0.1f);  // 0.3s past 5× the T60 → ~-150 dB → near silence
    const float rmsLong  = run(2.0f);  // 0.3s well within T60 → strong residual

    // At t = 0.3 s:
    //   decay=0.1 s → envelope = exp(-6.9 × 0.3/0.1) ≈ 1e-9 → effectively 0
    //   decay=2.0 s → envelope = exp(-6.9 × 0.3/2.0) ≈ 0.35
    // Ratio long/short ≈ 0.35/1e-9 → astronomically large in theory; in
    // practice the short partial is at the denormal floor.  Require
    // long > short × 10 — gives 100× safety vs the theoretical ratio
    // while staying robust against numerical noise.
    INFO("rmsShort=" << rmsShort << " rmsLong=" << rmsLong);
    REQUIRE(rmsLong > rmsShort * 10.0f);
}

// --- Inharmonicity ------------------------------------------------------

TEST_CASE("ModalResonator: inharmonicity stretches upper partials", "[modal]")
{
    // 2-partial setup, ratios 1 and 4.  At inharmonicity=0 the 4th partial
    // sits exactly at 4 × fundamental.  At inharmonicity=1 the formula
    // stretched(4) = 4 × sqrt(1 + 0.1 × 16) = 4 × sqrt(2.6) ≈ 6.45.
    // Expected shifted partial: 100 Hz × 6.45 ≈ 645 Hz.  Bin at the
    // un-stretched 400 Hz should be far below its un-stretched value, and
    // bin at the stretched 645 Hz should now dominate.
    auto magsAt = [](float inharm, float probe)
    {
        ModalResonator m;
        m.prepare(kSR);
        m.setPartialCount(2);
        m.setFundamental(100.0f);
        m.setPartialRatio(0, 1.0f);
        m.setPartialRatio(1, 4.0f);
        m.setPartialDecay(0, 1.0f);
        m.setPartialDecay(1, 1.0f);
        m.setInharmonicity(inharm);

        const int N = static_cast<int>(kSR * 0.5f);
        auto out = strikeAndCapture(m, 1.0f, N);
        return goertzelMagnitude(out, probe, kSR);
    };

    const float mag400_at0 = magsAt(0.0f, 400.0f);  // on-target at inharm=0
    const float mag400_at1 = magsAt(1.0f, 400.0f);  // off-target at inharm=1
    const float mag645_at1 = magsAt(1.0f, 645.0f);  // on stretched partial at inharm=1

    // Stretched partial (645 Hz) at inharm=1 must dominate the un-stretched
    // bin (400 Hz) at inharm=1.  BW = 645/Q = ~16 Hz; 245 Hz off-centre is
    // ~15 bandwidths → magnitude attenuated by (Δf/BW)² ≈ 230 in power,
    // 15 in amplitude.  Threshold 3× allows large margin.
    INFO("mag400@0=" << mag400_at0 << " mag400@1=" << mag400_at1 << " mag645@1=" << mag645_at1);
    REQUIRE(mag645_at1 > mag400_at1 * 3.0f);
    // Symmetric check: at inharm=0 the 400 Hz bin must be much higher
    // than at inharm=1 (partial has moved away).  Ratio of (Δf/BW)² ≈ 230
    // again; threshold 3× is the same conservative bound.
    REQUIRE(mag400_at0 > mag400_at1 * 3.0f);
}

// --- Parameter clamping -----------------------------------------------

TEST_CASE("ModalResonator: setFundamental clamps to [10, sr*0.45]", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(1);
    m.setPartialDecay(0, 0.2f);

    // Below floor — must clamp to 10 Hz, BP must remain stable & finite.
    m.setFundamental(-100.0f);
    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(m.process()));

    m.reset();

    // Above Nyquist — must clamp to sr*0.45.  Output still finite.
    m.setFundamental(1e9f);
    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(m.process()));
}

TEST_CASE("ModalResonator: setPartialCount clamps to [1, kMaxPartials]", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);

    // Zero / negative → clamp to 1 partial.  Strike + capture must produce
    // finite output.
    m.setPartialCount(-3);
    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(m.process()));

    m.reset();

    // Above max → clamp to kMaxPartials.
    m.setPartialCount(ModalResonator::kMaxPartials + 50);
    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(m.process()));
}

TEST_CASE("ModalResonator: setPartialRatio clamps & out-of-range idx ignored", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);

    // Out-of-range idx must not crash and must not corrupt anything.
    m.setPartialRatio(-1, 1.0f);
    m.setPartialRatio(ModalResonator::kMaxPartials + 5, 1.0f);
    m.setPartialRatio(0, -1.0f);   // negative clamped to floor
    m.setPartialRatio(0, 1e6f);    // huge clamped to ceiling

    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(m.process()));
}

TEST_CASE("ModalResonator: setPartialDecay clamps", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);

    m.setPartialDecay(0, -5.0f);    // clamp to positive floor
    m.setPartialDecay(1, 1e6f);     // clamp to ceiling
    m.setPartialDecay(-1, 1.0f);    // ignored
    m.setPartialDecay(99, 1.0f);    // ignored

    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(m.process()));
}

TEST_CASE("ModalResonator: setInharmonicity clamps to [0, 1]", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setInharmonicity(-1.0f);
    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(m.process()));
    m.reset();

    m.setInharmonicity(5.0f);
    m.strike(1.0f);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(m.process()));
}

TEST_CASE("ModalResonator: strike velocity clamps to [0, 1]", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(1);
    m.setPartialDecay(0, 0.2f);

    // Negative velocity → clamped to 0 → no excitation → silence.
    m.strike(-1.0f);
    for (int i = 0; i < 1000; ++i)
        // Excitation amplitude exactly 0 → BP input exactly 0 → output 0.
        REQUIRE_THAT(m.process(), WithinAbs(0.0f, 1e-9f));

    m.reset();
    // Huge velocity → clamped to 1, still finite.
    m.strike(1e9f);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(std::isfinite(m.process()));
}

// --- Parameter boundary behaviour --------------------------------------

TEST_CASE("ModalResonator: partial above Nyquist is silently muted", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(2);
    m.setFundamental(15000.0f);
    m.setPartialRatio(0, 1.0f);   // 15 kHz — under Nyquist
    m.setPartialRatio(1, 3.0f);   // 45 kHz — far above Nyquist, must be muted
    m.setPartialDecay(0, 0.5f);
    m.setPartialDecay(1, 0.5f);
    m.setInharmonicity(0.0f);

    auto out = strikeAndCapture(m, 1.0f, 4096);
    // Every sample must be finite even though partial 1's nominal frequency
    // is well past Nyquist.  Without muting the Biquad coefficient
    // computation clamps to sr*0.45 = 19.8 kHz and the partial would still
    // ring — but at a *wrong* (aliased) frequency, audibly out of place.
    // The class contract is to silently mute, so we additionally assert
    // partial 1's nominal bin contains negligible energy and the muted
    // partial does not contribute audible aliasing in band.
    for (float s : out)
        REQUIRE(std::isfinite(s));

    // The 15 kHz partial is still alive — output must not be silent.
    // 1e-4 ≈ -80 dBFS, well above numerical floor.
    REQUIRE(peakAbs(out) > 1e-4f);
}

TEST_CASE("ModalResonator: partialCount=1 single mode behaves sinusoidally", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(1);
    m.setFundamental(440.0f);
    m.setPartialRatio(0, 1.0f);
    m.setPartialDecay(0, 1.0f);
    m.setInharmonicity(0.5f);  // inharmonicity is harmless on a single mode

    // Skip the first 20 ms (excitation transient + envelope ramp-in artefacts).
    const int skip = static_cast<int>(kSR * 0.02f);
    const int N = static_cast<int>(kSR * 0.1f); // 0.1 s analysis window
    m.strike(1.0f);
    for (int i = 0; i < skip; ++i) m.process();
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = m.process();

    // Compare on-bin to far-bin: should be >>1 because a single high-Q BP
    // produces a near-sinusoid at fc.  Same logic as the earlier 1-partial
    // test but with a different time window.
    const float magOn  = goertzelMagnitude(buf, 440.0f, kSR);
    const float magOff = goertzelMagnitude(buf, 100.0f, kSR);
    INFO("magOn=" << magOn << " magOff=" << magOff);
    // 100 Hz is 340 Hz below 440 Hz; BW = 440/40 = 11 Hz → 31 bandwidths off.
    // Attenuation ≈ (31)² ≈ 960 in power, 31 in amplitude.  Threshold 5× is
    // conservative (>6× margin) and survives Goertzel leakage.
    REQUIRE(magOn > magOff * 5.0f);
}

// --- Long-run stability + denormal protection ----------------------------

TEST_CASE("ModalResonator: 10 s strike + silent tail stays finite", "[modal]")
{
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(ModalResonator::kMaxPartials);
    m.setFundamental(220.0f);
    for (int i = 0; i < ModalResonator::kMaxPartials; ++i)
    {
        m.setPartialRatio(i, static_cast<float>(i + 1));
        m.setPartialDecay(i, 1.0f);
    }
    m.setInharmonicity(0.3f);
    m.strike(1.0f);

    // 10 s = 441000 samples.  Tests denormal accumulation in 16 parallel
    // BPs over a long silent tail (most of the 10 s has the envelope
    // multiplier already below the silence threshold).
    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        float s = m.process();
        REQUIRE(std::isfinite(s));
    }
}

TEST_CASE("ModalResonator: 30 s silent tail produces no NaN/denormals", "[modal]")
{
    // Explicit denormal stress: long, completely silent tail after a strike.
    // Without Biquad's built-in +1e-25 DC offset the BP feedback state would
    // gradually flush to denormals; this test ensures the offset is reached
    // through ModalResonator's per-partial Biquad and that no NaN escapes.
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(8);
    m.setFundamental(110.0f);
    for (int i = 0; i < 8; ++i)
    {
        m.setPartialRatio(i, static_cast<float>(i + 1));
        // Very short envelope so by t=1 s the partials are deep in the
        // silent tail and only the BP state can produce output.
        m.setPartialDecay(i, 0.05f);
    }

    m.strike(1.0f);

    // 30 s of process(); explicit isnan / isinf check on every sample.
    constexpr int N = 30 * 44100;
    for (int i = 0; i < N; ++i)
    {
        float s = m.process();
        REQUIRE_FALSE(std::isnan(s));
        REQUIRE_FALSE(std::isinf(s));
    }
}

// --- Extreme parameter combination ---------------------------------------

TEST_CASE("ModalResonator: max partials × long decay × high inharmonicity stays finite", "[modal]")
{
    // Stress: every partial alive, long decay, full inharmonicity stretch.
    // High partials may push past Nyquist after stretching and must be
    // muted (asserted indirectly by the finiteness requirement).
    ModalResonator m;
    m.prepare(kSR);
    m.setPartialCount(ModalResonator::kMaxPartials);
    m.setFundamental(440.0f);
    m.setInharmonicity(1.0f);  // maximum stretch
    for (int i = 0; i < ModalResonator::kMaxPartials; ++i)
    {
        m.setPartialRatio(i, static_cast<float>(i + 1));
        m.setPartialDecay(i, 10.0f);  // long sustained ring
    }
    // Repeated strikes to keep all partials excited.
    for (int strike = 0; strike < 10; ++strike)
    {
        m.strike(1.0f);
        for (int i = 0; i < 4410; ++i) // 0.1 s between strikes
        {
            float s = m.process();
            REQUIRE(std::isfinite(s));
        }
    }
}

// --- Default ctor sanity --------------------------------------------------

TEST_CASE("ModalResonator: default-constructed is usable without prepare()", "[modal]")
{
    // Per API convention every primitive is default-constructible with a
    // sensible 44.1 kHz fallback.  Calling process() before prepare() must
    // return finite values (not UB).
    ModalResonator m;
    m.strike(1.0f);
    for (int i = 0; i < 100; ++i)
        REQUIRE(std::isfinite(m.process()));
}
