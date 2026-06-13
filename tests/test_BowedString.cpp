#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/BowedString.h>
#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace ideath;

static constexpr float kSR = 44100.0f;
static constexpr float kPi = 3.14159265358979323846f;

// Goertzel single-bin amplitude over `buf`.
static float goertzelMagnitude(const std::vector<float>& buf, float freq, float sr)
{
    const int N = static_cast<int>(buf.size());
    const float w = 2.0f * kPi * freq / sr;
    const float coeff = 2.0f * std::cos(w);
    float s1 = 0.0f, s2 = 0.0f;
    for (int n = 0; n < N; ++n)
    {
        const float s0 = buf[n] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const float real = s1 - s2 * std::cos(w);
    const float imag = s2 * std::sin(w);
    return std::sqrt(real * real + imag * imag) * 2.0f / static_cast<float>(N);
}

static float rms(const std::vector<float>& buf, int start, int count)
{
    float s = 0.0f;
    for (int i = start; i < start + count; ++i) s += buf[i] * buf[i];
    return std::sqrt(s / static_cast<float>(count));
}

static float peakAbs(const std::vector<float>& buf)
{
    float m = 0.0f;
    for (float v : buf) m = std::max(m, std::fabs(v));
    return m;
}

static std::vector<float> capture(BowedString& s, int samples)
{
    std::vector<float> out(samples);
    for (int i = 0; i < samples; ++i) out[i] = s.process();
    return out;
}

// --- Default state / silence --------------------------------------------

TEST_CASE("BowedString: silent before bow is engaged", "[bowed]")
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(220.0f);
    // Default bowVelocity=0 + zero initial loop state → output ≈ 0.
    // No friction force (v_rel = 0 - 0 = 0 → f = 0); the delay-line write
    // path adds +1e-25f for denormal protection (CLAUDE.md convention),
    // so after the loop has circulated once or twice the read pulls back
    // a residual on that order.  Bound 1e-10 is 1e15× above that floor
    // and 1e10× below any musically meaningful level — same threshold
    // KarplusStrong uses for its silence test (AUDIT.md test_KarplusStrong).
    auto out = capture(s, 1000);
    for (float v : out)
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));
}

// --- Reset / state ------------------------------------------------------

TEST_CASE("BowedString: reset clears loop and is deterministic", "[bowed]")
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(220.0f);
    s.setBowVelocity(0.5f);
    s.setPressure(0.5f);

    auto first = capture(s, 4410);

    s.reset();
    auto second = capture(s, 4410);

    // process() is fully deterministic — no internal RNG, no clock-based
    // state.  reset() zeroes every delay-line slot and the filter memory,
    // so the second pass must reproduce the first sample-for-sample.
    for (size_t i = 0; i < first.size(); ++i)
        REQUIRE(first[i] == second[i]);
}

TEST_CASE("BowedString: prepare() reinitialises and silences any tail", "[bowed]")
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(220.0f);
    s.setBowVelocity(1.0f);
    s.setPressure(0.7f);
    for (int i = 0; i < 4410; ++i) s.process();  // build up loop energy

    // prepare() reallocates the delay buffers and zeroes state.  After
    // re-prepare with bow held at 0, the output must be exact zero — same
    // contract as the "silent before engaged" test.
    s.prepare(kSR);
    s.setFrequency(220.0f);
    s.setBowVelocity(0.0f);
    s.setPressure(0.0f);
    auto out = capture(s, 1000);
    // Same DelayLine-anti-denormal residual as the "silent before engaged"
    // test — 1e-10 is the matching floor.
    for (float v : out)
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));
}

// --- Expected behaviour: produces a tone at the fundamental -------------

TEST_CASE("BowedString: bow-driven oscillation has energy at the fundamental", "[bowed]")
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(220.0f);
    s.setPressure(0.6f);
    s.setPosition(0.1f);    // close to bridge — minimal comb attenuation
    s.setDamping(0.2f);
    s.setBowVelocity(0.5f);

    // Skip the attack transient (loop build-up). 1 s is generous: at
    // freq=220 Hz and a contractive loop, the steady-state amplitude is
    // approached within a few periods of the friction-driven oscillation,
    // well under 100 ms.  We discard 1 s to be robust to position / damping
    // edge cases.
    for (int i = 0; i < static_cast<int>(kSR); ++i) s.process();

    // Capture 1 s of steady-state and probe the fundamental bin (bin-aligned
    // at 220 cycles over 44100 samples).
    auto out = capture(s, static_cast<int>(kSR));
    const float magFund = goertzelMagnitude(out, 220.0f, kSR);
    const float magOff  = goertzelMagnitude(out, 173.0f, kSR);  // non-harmonic

    // Bowed string is a self-oscillating loop locked to D = sr / freq, so
    // the fundamental MUST dominate any non-harmonic bin.  Ratio 5 is a
    // conservative margin against measurement noise; in practice the
    // separation is much larger (the friction-driven Helmholtz motion is
    // strongly periodic).
    REQUIRE(magFund > magOff * 5.0f);
    // Fundamental must clear an audibility floor: −40 dBFS ≈ 0.01 is well
    // above any numerical denormal-protection floor (1e-25) and well below
    // the typical bowed-amplitude range (~0.3–1.0).
    REQUIRE(magFund > 0.01f);
}

// --- Bow off → string rings out (decay) ---------------------------------

TEST_CASE("BowedString: rings out after bow disengages", "[bowed]")
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(220.0f);
    s.setPressure(0.6f);
    s.setPosition(0.1f);
    s.setDamping(0.3f);
    s.setBowVelocity(0.5f);

    // 0.5 s of bowing to build up steady oscillation.
    for (int i = 0; i < static_cast<int>(kSR * 0.5f); ++i) s.process();

    // Now release the bow: friction-driven excitation goes to zero, the
    // loop should ring out under the LP-controlled damping.
    s.setBowVelocity(0.0f);
    s.setPressure(0.0f);

    // Sample 100 ms windows at t=0 (just after release) and t=1 s.
    const int win = static_cast<int>(kSR * 0.1f);
    auto out = capture(s, static_cast<int>(kSR));
    const float rmsEarly = rms(out, 0,                     win);
    const float rmsLate  = rms(out, static_cast<int>(out.size()) - win, win);

    // After release, the loop is purely contractive (loopGain × LP gain < 1),
    // so the tail must decay monotonically in RMS over the 1 s window.
    // Ratio 2× is a soft floor: at damping=0.3 the typical -60 dB decay
    // is on the order of a few seconds, so a 1 s decay should give at
    // least 6 dB drop (factor 2 in amplitude / RMS).  Loose enough to
    // survive damping value drift but strict enough to catch a "loop
    // gain stuck at 1" bug.
    REQUIRE(rmsEarly > rmsLate * 2.0f);
}

TEST_CASE("BowedString: higher damping → faster ringout", "[bowed]")
{
    auto ringoutRms = [&](float damping)
    {
        BowedString s;
        s.prepare(kSR);
        s.setFrequency(220.0f);
        s.setPressure(0.6f);
        s.setPosition(0.1f);
        s.setDamping(damping);
        s.setBowVelocity(0.5f);
        for (int i = 0; i < static_cast<int>(kSR * 0.5f); ++i) s.process();
        s.setBowVelocity(0.0f);
        s.setPressure(0.0f);
        // 200 ms after release — long enough that the LP-controlled decay
        // has separated the two damping cases.
        for (int i = 0; i < static_cast<int>(kSR * 0.2f); ++i) s.process();
        auto out = capture(s, static_cast<int>(kSR * 0.1f));
        return rms(out, 0, static_cast<int>(out.size()));
    };

    const float rmsLow  = ringoutRms(0.05f);
    const float rmsHigh = ringoutRms(0.9f);

    // Higher LP damping → more HF loss per loop → faster decay.  At 200 ms
    // after release, high-damping RMS must be strictly lower.  The ratio
    // 2× is the same soft floor used above — catches "damping ignored" /
    // "filter coefs swapped" without fitting to a specific loop-gain value.
    REQUIRE(rmsLow > rmsHigh * 2.0f);
}

// --- Position changes the spectral comb ---------------------------------

TEST_CASE("BowedString: position=0.5 notches the 2nd harmonic", "[bowed]")
{
    auto bowAndCapture = [&](float position)
    {
        BowedString s;
        s.prepare(kSR);
        s.setFrequency(220.0f);
        s.setPressure(0.6f);
        s.setPosition(position);
        s.setDamping(0.2f);
        s.setBowVelocity(0.5f);
        for (int i = 0; i < static_cast<int>(kSR); ++i) s.process();  // settle
        return capture(s, static_cast<int>(kSR));
    };

    auto outClose = bowAndCapture(0.1f);  // pickup near bridge: notches at 10x freq, out-of-band
    auto outMid   = bowAndCapture(0.5f);  // pickup at mid-string: notch at 2× freq exactly

    // Comb response at the n-th harmonic for pickup fraction p:
    //   |H(n)| = 2 |sin(π × n × p)|
    // At p = 0.1, n = 2:  |H| = 2 sin(0.2π) ≈ 1.176
    // At p = 0.5, n = 2:  |H| = 2 sin(π)     = 0          ← full notch
    // The notch is analytically infinite; finite leakage (windowing,
    // friction nonlinearity adding inter-harmonic content) limits the
    // observed ratio.  Threshold 10× is the standard "notch is real"
    // separation used elsewhere in the suite (e.g. ModalResonator BP
    // off-bin tests); chosen to survive non-ideal harmonic content
    // without fitting to a specific loop-gain residual.
    const float mag2Close = goertzelMagnitude(outClose, 440.0f, kSR);
    const float mag2Mid   = goertzelMagnitude(outMid,   440.0f, kSR);
    INFO("mag2Close=" << mag2Close << "  mag2Mid=" << mag2Mid);
    REQUIRE(mag2Close > mag2Mid * 10.0f);
}

// --- Output range --------------------------------------------------------

TEST_CASE("BowedString: output bounded under any bowing", "[bowed]")
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(220.0f);
    s.setPressure(1.0f);
    s.setPosition(0.5f);   // comb output can briefly exceed ±1 for ringy harmonics
    s.setDamping(0.0f);    // no LP loss — most aggressive loop
    s.setBowVelocity(0.3f); // near the friction-curve peak — drives the loop hard

    auto out = capture(s, static_cast<int>(kSR * 2.0f));

    // Loop input is tanh-saturated → bounded by ±1.0 INSIDE the loop.
    // The output is `mainTap − pickupTap`, so peak ≤ 2.0 by triangle
    // inequality (each tap is in [−1, 1]).  Margin 1e-3 for the linear-
    // interpolated DelayLine read's ULP error and float-sum precision.
    REQUIRE(peakAbs(out) <= 2.0f + 1e-3f);
}

// --- Clamping -----------------------------------------------------------

TEST_CASE("BowedString: parameter clamping", "[bowed]")
{
    BowedString s;
    s.prepare(kSR);

    // Frequency below kMinFreq and above sr × 0.45 → clamped.
    s.setFrequency(0.0f);
    s.setFrequency(-100.0f);
    s.setFrequency(1e6f);
    s.setFrequency(kSR * 0.6f);

    // Bow velocity [-1, 1] — negative values allowed (down-bow), but
    // outside the unit range must be clamped.
    s.setBowVelocity(-5.0f);
    s.setBowVelocity(5.0f);

    // Pressure [0, 1].
    s.setPressure(-1.0f);
    s.setPressure(10.0f);

    // Position [kMinPosition, kMaxPosition].
    s.setPosition(-1.0f);
    s.setPosition(2.0f);

    // Damping [0, 1].
    s.setDamping(-1.0f);
    s.setDamping(10.0f);

    // None of the above must NaN / explode.  Process to confirm finite output.
    auto out = capture(s, 4410);
    for (float v : out)
    {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::fabs(v) <= 2.0f + 1e-3f);
    }
}

// --- Long-run stability -------------------------------------------------

TEST_CASE("BowedString: 10s stability under continuous bowing", "[bowed][stability]")
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(220.0f);
    s.setPressure(0.8f);
    s.setPosition(0.3f);
    s.setDamping(0.1f);
    s.setBowVelocity(0.7f);

    // 10 s = 441000 samples.  Delay-line denormal protection (DelayLine
    // applies +1e-25f DC) plus tanh saturation must keep the loop bounded
    // even at heavy pressure and minimal damping over a long run.
    const int N = 10 * static_cast<int>(kSR);
    for (int n = 0; n < N; ++n)
    {
        float v = s.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(std::fabs(v) <= 2.0f + 1e-3f);
    }
}

// --- Extreme parameter combos ------------------------------------------

TEST_CASE("BowedString: extreme combos stay finite", "[bowed][extreme]")
{
    BowedString s;
    s.prepare(kSR);

    SECTION("max pressure × min damping × max bow")
    {
        s.setFrequency(110.0f);
        s.setPressure(1.0f);
        s.setPosition(0.5f);
        s.setDamping(0.0f);
        s.setBowVelocity(1.0f);
        auto out = capture(s, 88200);
        for (float v : out)
        {
            REQUIRE(std::isfinite(v));
            REQUIRE(std::fabs(v) <= 2.0f + 1e-3f);
        }
    }

    SECTION("zero pressure + high bow velocity → silence-or-decay")
    {
        s.setFrequency(220.0f);
        s.setPressure(0.0f);
        s.setPosition(0.1f);
        s.setDamping(0.5f);
        s.setBowVelocity(1.0f);
        // With pressure=0 the friction force is identically zero (pressure
        // multiplies f).  No energy is injected; an already-empty loop
        // stays at DelayLine's +1e-25 denormal-protection floor.
        auto out = capture(s, 4410);
        for (float v : out)
            REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));
    }

    // Note on the audit-suggested peer property "setBowVelocity(0) + high
    // pressure → silence": that property does NOT hold for this model
    // and is documented in BowedString.h as the "stuck-bow self-excitation"
    // quirk.  With v_bow = 0 and v_string small, the friction term
    // f = pressure × scale × (0 − v_string) × exp(−k|v_string|)
    // linearises to f ≈ −13.59 × v_string for small v_string.  Combined
    // with the contractive loopGain ≈ 0.999, the loop's small-signal
    // gain is `loopGain − 13.59 ≈ −12.6`, magnitude well above 1 →
    // positive feedback (sign-flipped each loop traversal).  The 1-port
    // analytical curve has no static-friction damping at v_bow = 0, so
    // any 1e-25 denormal residual is amplified into a self-sustained
    // oscillation at ±tanh-saturated amplitude.  A real bowed string
    // damps under a stuck bow (static friction holds the string in place);
    // this is a v1 acceptance-bar limitation (no LUT, no separate stick
    // regime).  v2 with a tabulated friction LUT would fix it.

    SECTION("very high frequency near Nyquist guard")
    {
        // kSR × 0.45 = 19845 Hz — the clamp ceiling.  delaySamples ≈ 2.22,
        // close to the fractional-delay floor.  Must not NaN.
        s.setFrequency(19000.0f);
        s.setPressure(0.7f);
        s.setPosition(0.3f);
        s.setDamping(0.4f);
        s.setBowVelocity(0.5f);
        auto out = capture(s, 4410);
        for (float v : out)
        {
            REQUIRE(std::isfinite(v));
            REQUIRE(std::fabs(v) <= 2.0f + 1e-3f);
        }
    }
}
