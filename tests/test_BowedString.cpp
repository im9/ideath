#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/BowedString.h>
#include "PitchAnalysis.h"
#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace ideath;

static constexpr float kSR = 44100.0f;
static constexpr double kPiD = 3.14159265358979323846;

namespace {

// Single-bin DFT amplitude of `buf` at `freq` (rectangular window).
double binAmplitude(const std::vector<float>& buf, double freq)
{
    double re = 0.0, im = 0.0;
    const double w = 2.0 * kPiD * freq / kSR;
    for (size_t n = 0; n < buf.size(); ++n)
    {
        re += buf[n] * std::cos(w * static_cast<double>(n));
        im += buf[n] * std::sin(w * static_cast<double>(n));
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(buf.size());
}

double rms(const std::vector<float>& buf, size_t start, size_t count)
{
    double s = 0.0;
    for (size_t i = start; i < start + count; ++i) s += double(buf[i]) * buf[i];
    return std::sqrt(s / static_cast<double>(count));
}

float peakAbs(const std::vector<float>& buf)
{
    float m = 0.0f;
    for (float v : buf) m = std::max(m, std::fabs(v));
    return m;
}

std::vector<float> capture(BowedString& s, size_t samples)
{
    std::vector<float> out(samples);
    for (auto& v : out) v = s.process();
    return out;
}

double runRms(BowedString& s, size_t samples)
{
    double e = 0.0;
    for (size_t i = 0; i < samples; ++i) { const double v = s.process(); e += v * v; }
    return std::sqrt(e / static_cast<double>(samples));
}

struct Patch { float f0, position, damping, pressure, velocity; };

BowedString bowed(const Patch& p)
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(p.f0);
    s.setPosition(p.position);
    s.setDamping(p.damping);
    s.setPressure(p.pressure);
    s.setBowVelocity(p.velocity);
    return s;
}

// The four pitches of the acceptance criteria: C2 / C4 / E4 / C6.
constexpr float kPitches[] = { 65.4f, 261.6f, 329.6f, 1046.5f };

// Contract for the output ceiling, from the CLAUDE.md output-level table.
// The model has no closed-form amplitude bound — the Helmholtz bridge wave
// grows as v/(2β) toward the bridge and Raman ripples ride on top — so the
// table states the measured envelope over every pitch / position / damping
// / pressure corner at full bow speed (2.8) with headroom.
constexpr float kOutputCeiling = 3.0f;

} // namespace

// --- Default state / silence --------------------------------------------

TEST_CASE("BowedString: silent before bow is engaged", "[bowed]")
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(220.0f);
    s.setPressure(1.0f);
    // Bow speed 0 → stick width w = p·kStickWidth·0 = 0 → the junction adds
    // nothing; the only content is the +1e-25 anti-denormal offset written
    // with every sample, which circulates with |gain| ≤ 1 and stays many
    // orders below 1e-10 (the floor KarplusStrong uses for the same test).
    auto out = capture(s, 4410);
    for (float v : out)
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));
}

// --- Reset / state ------------------------------------------------------

TEST_CASE("BowedString: reset clears loop and is deterministic", "[bowed]")
{
    auto s = bowed({ 220.0f, 0.1f, 0.2f, 0.5f, 0.5f });
    auto first = capture(s, 4410);
    s.reset();
    auto second = capture(s, 4410);
    // No RNG, no clock; reset() zeroes both buffers, the low-pass memory
    // and the bow-speed smoother, so the replay is bit-identical.
    for (size_t i = 0; i < first.size(); ++i)
        REQUIRE(first[i] == second[i]);
}

TEST_CASE("BowedString: prepare() reinitialises and silences any tail", "[bowed]")
{
    auto s = bowed({ 220.0f, 0.1f, 0.2f, 0.7f, 1.0f });
    for (int i = 0; i < 4410; ++i) s.process();
    s.prepare(kSR);
    s.setBowVelocity(0.0f);
    s.setPressure(0.0f);
    // Same 1e-10 floor as the "silent before engaged" test.
    auto out = capture(s, 1000);
    for (float v : out)
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));
}

// --- Tuning ---------------------------------------------------------------

TEST_CASE("BowedString: steady tone is in tune and harmonic", "[bowed][tuning]")
{
    // Acceptance criteria.  A single loop that applies the friction map
    // once per period period-doubles: it puts nearly all its energy on
    // (n+½)·f0 at onset pressures and goes chaotic above them.  These
    // criteria rule that out:
    //
    //  (1) Median over partials 1-3 of the resonance-peak position is within
    //      ±10 cents of f0.  10 cents is the upper end of the 5-10 cent range
    //      within which a sustained tone stops being heard as out of tune.
    //  (2) ≥ 80 % of the energy above 40 Hz lies within ±25 cents of n·f0.
    //      25 cents is half the 50-cent boundary to the neighbouring
    //      semitone; the remaining 20 % leaves room for bow noise and jitter.
    //  (3) Energy within ±25 cents of (n+½)·f0 is ≤ 1/10 of the harmonic
    //      energy: a partial series 10 dB down does not take the pitch over.
    //
    // Grid: the four pitches; bow position from the bridge-most setting
    // (0.02) through 0.068 / 0.212 to 0.3 and the top of the range
    // (0.45); damping 0 / 0.2 / 0.5 / 0.8 / 1; pressure just above the
    // Schelleng threshold (1.3 × minimumPressure), at the geometric middle of
    // the speaking range and at 1; bow speed at both ends of 0.05-0.85.
    //
    // Measurement: 2 s settle (the onset at 1.3 × threshold grows with an
    // e-folding time of about a second), then a 5.9 s Hann-windowed FFT
    // (0.17 Hz bins → ±25 cents spans ≥ 5 bins even at C2); see
    // PitchAnalysis.h.
    using namespace pitch_analysis;
    constexpr size_t kN = size_t(1) << 18;
    const double binHz = kSR / static_cast<double>(kN);
    const size_t settle = static_cast<size_t>(2.0f * kSR);

    for (float f0 : kPitches)
        for (float position : { 0.02f, 0.068f, 0.212f, 0.3f, 0.45f })
            for (float damping : { 0.0f, 0.2f, 0.5f, 0.8f, 1.0f })
            {
                const float pMin = BowedString::minimumPressure(kSR, f0, position, damping);
                // Every cell of the grid must speak inside the pressure range
                // with the 1.3× margin; none is skipped.
                REQUIRE(pMin < 1.0f / 1.3f);
                const float pLow = 1.3f * pMin;
                for (float pressure : { pLow, std::sqrt(pLow), 1.0f })
                    for (float velocity : { 0.05f, 0.85f })
                    {
                        auto s = bowed({ f0, position, damping, pressure, velocity });
                        for (size_t i = 0; i < settle; ++i) s.process();
                        auto buf = capture(s, kN);
                        const auto p = powerSpectrum(buf, kN);
                        const auto pitch = partialPitch(p, binHz, f0);
                        const auto h = harmonicity(p, binHz, f0);
                        INFO("f0=" << f0 << " pos=" << position << " damp=" << damping
                             << " p=" << pressure << " (pMin " << pMin << ") v=" << velocity
                             << " partials(c)=" << pitch.partialCents[0] << ", "
                             << pitch.partialCents[1] << ", " << pitch.partialCents[2]
                             << " harmonic=" << h.harmonicFraction()
                             << " half/harmonic=" << h.halfToHarmonic());
                        CHECK(std::fabs(pitch.medianCents) <= 10.0);
                        CHECK(h.harmonicFraction() >= 0.8);
                        CHECK(h.halfToHarmonic() <= 0.1);
                    }
            }
}

TEST_CASE("BowedString: reference patches are in tune and harmonic",
          "[bowed][tuning]")
{
    // Two plugin patches (bow speed / position / damping) at pressures from
    // the onset of speaking to full.  Same criteria and derivations as the
    // grid test above.
    using namespace pitch_analysis;
    constexpr size_t kN = size_t(1) << 18;
    const double binHz = kSR / static_cast<double>(kN);

    struct Setting { float v, pos, damp; float pressures[3]; };
    const Setting settings[] = {
        { 0.37f, 0.212f, 0.495f, { 0.3f, 0.5f, 0.95f } },
        { 0.69f, 0.068f, 0.18f,  { 0.6f, 0.8f, 1.0f } },
    };
    for (const auto& st : settings)
        for (float pressure : st.pressures)
            for (float f0 : kPitches)
            {
                auto s = bowed({ f0, st.pos, st.damp, pressure, st.v });
                for (int i = 0; i < static_cast<int>(2.0f * kSR); ++i) s.process();
                auto buf = capture(s, kN);
                const auto p = powerSpectrum(buf, kN);
                const auto pitch = partialPitch(p, binHz, f0);
                const auto h = harmonicity(p, binHz, f0);
                INFO("f0=" << f0 << " v=" << st.v << " pos=" << st.pos << " damp=" << st.damp
                     << " p=" << pressure << " median(c)=" << pitch.medianCents
                     << " harmonic=" << h.harmonicFraction()
                     << " half/harmonic=" << h.halfToHarmonic());
                CHECK(std::fabs(pitch.medianCents) <= 10.0);
                CHECK(h.harmonicFraction() >= 0.8);
                CHECK(h.halfToHarmonic() <= 0.1);
            }
}

// --- Schelleng minimum pressure -------------------------------------------

TEST_CASE("BowedString: minimum pressure separates silence from speaking",
          "[bowed][threshold]")
{
    // minimumPressure() is the pressure at which the sliding rest state
    // loses linear stability.  Below it any attack transient dies away;
    // above it the string's oscillation grows into Helmholtz motion and
    // holds.
    //
    // Bracket ×0.8 / ×1.25 — far enough from the threshold that the net
    // growth / decay shows inside the measurement span, close enough that
    // a threshold off by more than 20-25 % fails.
    //
    // Criterion: rms over [9.5, 10] s vs rms over [2, 2.5] s, after the bow
    // has been on since t = 0.  Speaking: the later window is within 0.9×
    // (−0.9 dB) of the earlier one or above — the tone holds or is still
    // growing.  Silent: the later window is below 0.9× — it decays.  The
    // 0.9 boundary sits between the two: a held tone does not lose level,
    // while near the threshold the net decay rate of the sub-threshold
    // transient scales roughly with (1 − p / pMin), so at 0.8 × pMin it is
    // ≥ 20 % of the free string's rate, i.e. ≥ 0.2 × 60 dB × 7.5 s /
    // 10 s = 9 dB over the span even at the longest decay (damping 0).
    const size_t win = static_cast<size_t>(0.5f * kSR);
    for (float f0 : kPitches)
        for (float position : { 0.068f, 0.212f, 0.45f })
            for (float damping : { 0.0f, 0.2f, 0.5f, 0.8f })
            {
                const float pMin = BowedString::minimumPressure(kSR, f0, position, damping);
                REQUIRE(pMin > 0.0f);
                REQUIRE(pMin < 0.8f);
                for (float factor : { 0.8f, 1.25f })
                {
                    auto s = bowed({ f0, position, damping, pMin * factor, 0.5f });
                    runRms(s, static_cast<size_t>(2.0f * kSR));
                    const double early = runRms(s, win);
                    runRms(s, static_cast<size_t>(7.0f * kSR));
                    const double late = runRms(s, win);
                    INFO("f0=" << f0 << " pos=" << position << " damp=" << damping
                         << " pMin=" << pMin << " factor=" << factor
                         << " early=" << early << " late=" << late);
                    if (factor < 1.0f) CHECK(late < 0.9 * early);
                    else               CHECK(late >= 0.9 * early);
                }
            }
}

TEST_CASE("BowedString: minimum pressure does not depend on bow speed",
          "[bowed][threshold]")
{
    // The stick width scales with |v|, so the whole motion scales with the
    // bow speed and the threshold cannot depend on it: at 1.25 × pMin the
    // tone speaks at every speed in 0.05-0.85, at 0.8 × it is silent at
    // every speed.  Same windows / criterion as the test above.
    const float f0 = 261.6f, position = 0.212f, damping = 0.495f;
    const float pMin = BowedString::minimumPressure(kSR, f0, position, damping);
    const size_t win = static_cast<size_t>(0.5f * kSR);
    for (float velocity : { 0.05f, 0.2f, 0.5f, 0.85f })
        for (float factor : { 0.8f, 1.25f })
        {
            auto s = bowed({ f0, position, damping, pMin * factor, velocity });
            runRms(s, static_cast<size_t>(2.0f * kSR));
            const double early = runRms(s, win);
            runRms(s, static_cast<size_t>(7.0f * kSR));
            const double late = runRms(s, win);
            INFO("v=" << velocity << " factor=" << factor << " early=" << early << " late=" << late);
            if (factor < 1.0f) CHECK(late < 0.9 * early);
            else               CHECK(late >= 0.9 * early);
        }
}

TEST_CASE("BowedString: member minimumPressure matches the static form", "[bowed][threshold]")
{
    auto s = bowed({ 329.6f, 0.068f, 0.18f, 0.5f, 0.5f });
    REQUIRE(s.minimumPressure() == BowedString::minimumPressure(kSR, 329.6f, 0.068f, 0.18f));
    // Clamped inputs give the clamped result.
    REQUIRE(BowedString::minimumPressure(kSR, 329.6f, 0.9f, 2.0f)
            == BowedString::minimumPressure(kSR, 329.6f, BowedString::kMaxPosition, 1.0f));
}

// --- Bow off → string rings out at the damping's decay time ---------------

TEST_CASE("BowedString: rings out at the decay time set by damping", "[bowed][decay]")
{
    // After the bow lets go (speed → 0, stick width → 0) the string is a
    // passive loop whose bridge gain is set so the fundamental falls 60 dB
    // in decay = (1 − d)·kMaxDecaySec + d·kMinDecaySec seconds, with the
    // low-pass and the interpolator's loss at f0 compensated.  Measured on
    // the fundamental (single-bin DFT, 0.1 s windows → 10 Hz resolution,
    // far from the 2nd harmonic at 440 Hz) between 0.2 s and 1.2 s after
    // release; expected drop 60 dB × 1 s / decay.
    //
    // Tolerance ±1 dB: the just-noticeable level step.  The leakage of the
    // other partials into a 0.1 s window around f0 is below −40 dB of the
    // fundamental (rectangular-window sidelobes 1/(πk) at k ≥ 22 bins), so
    // it moves either window by < 0.1 dB.
    for (float damping : { 0.0f, 0.3f, 0.7f, 1.0f })
    {
        auto s = bowed({ 220.0f, 0.1f, damping, 0.6f, 0.5f });
        for (int i = 0; i < static_cast<int>(kSR); ++i) s.process();
        s.setBowVelocity(0.0f);
        // Smoother time constant 5 ms: after 0.2 s the bow speed is
        // e^-40 of where it started — fully off.
        for (int i = 0; i < static_cast<int>(0.2f * kSR); ++i) s.process();
        auto a = capture(s, static_cast<size_t>(0.1f * kSR));
        for (int i = 0; i < static_cast<int>(0.9f * kSR); ++i) s.process();
        auto b = capture(s, static_cast<size_t>(0.1f * kSR));
        const double dropDb = 20.0 * std::log10(binAmplitude(a, 220.0) / binAmplitude(b, 220.0));
        const double decay = (1.0 - damping) * BowedString::kMaxDecaySec
                           + damping * BowedString::kMinDecaySec;
        INFO("damping=" << damping << " drop=" << dropDb << " dB, expected " << 60.0 / decay);
        CHECK_THAT(dropDb, WithinAbs(60.0 / decay, 1.0));
    }
}

// --- Bow position ---------------------------------------------------------

TEST_CASE("BowedString: bowing at 1/n of the string suppresses harmonic n", "[bowed]")
{
    // Helmholtz motion excited at β drives harmonic n in proportion to
    // sin(nπβ): bowing at β = 1/4 puts a node of the 4th harmonic under
    // the bow.  At 220 Hz the bridge segment is β·L ≈ 50 samples, so the
    // rounding to whole samples moves β by at most 0.5/200 → |sin(4π·β)| ≤
    // sin(4π·0.0025) = 0.031, against sin(4π·0.2) = 0.588 at β = 0.2: a
    // 19× ratio.  Required: ≥ 10× (20 dB), leaving room for the corner
    // rounding and Raman ripple of a non-ideal Helmholtz motion.
    auto harmonic = [](float position, int n) {
        auto s = bowed({ 220.0f, position, 0.2f, 0.5f, 0.5f });
        for (int i = 0; i < static_cast<int>(kSR); ++i) s.process();
        auto buf = capture(s, static_cast<size_t>(kSR));
        return binAmplitude(buf, 220.0 * n);
    };
    const double h4AtQuarter = harmonic(0.25f, 4);
    const double h4AtFifth   = harmonic(0.2f, 4);
    INFO("H4 at 1/4: " << h4AtQuarter << "  H4 at 1/5: " << h4AtFifth);
    REQUIRE(h4AtFifth > 10.0 * h4AtQuarter);
}

// --- Output range ---------------------------------------------------------

TEST_CASE("BowedString: output stays under the documented ceiling", "[bowed]")
{
    // Every pitch / position / damping / pressure corner at full bow speed,
    // with a bow reversal (+1 → −1) and a release, against the CLAUDE.md
    // contract kOutputCeiling (see its derivation at the top of the file).
    for (float f0 : { 30.0f, 65.4f, 261.6f, 1046.5f, 4000.0f, 19000.0f })
        for (float position : { 0.02f, 0.068f, 0.212f, 0.45f })
            for (float damping : { 0.0f, 0.5f, 1.0f })
                for (float pressure : { 0.1f, 0.3f, 1.0f })
                {
                    auto s = bowed({ f0, position, damping, pressure, 1.0f });
                    float peak = 0.0f;
                    for (int i = 0; i < static_cast<int>(3.0f * kSR); ++i)
                    {
                        if (i == static_cast<int>(kSR))        s.setBowVelocity(-1.0f);
                        if (i == static_cast<int>(2.0f * kSR)) s.setBowVelocity(0.0f);
                        const float v = s.process();
                        REQUIRE(std::isfinite(v));
                        peak = std::max(peak, std::fabs(v));
                    }
                    INFO("f0=" << f0 << " pos=" << position << " damp=" << damping
                         << " p=" << pressure << " peak=" << peak);
                    CHECK(peak <= kOutputCeiling);
                }
}

// --- Clamping -----------------------------------------------------------

TEST_CASE("BowedString: parameter clamping", "[bowed]")
{
    BowedString s;
    s.prepare(kSR);
    s.setFrequency(0.0f);
    s.setFrequency(-100.0f);
    s.setFrequency(1e6f);
    s.setFrequency(kSR * 0.6f);
    s.setBowVelocity(-5.0f);
    s.setBowVelocity(5.0f);
    s.setPressure(-1.0f);
    s.setPressure(10.0f);
    s.setPosition(-1.0f);
    s.setPosition(2.0f);
    s.setDamping(-1.0f);
    s.setDamping(10.0f);
    auto out = capture(s, 44100);
    for (float v : out)
    {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::fabs(v) <= kOutputCeiling);
    }

    // Clamped values behave exactly like the limits themselves.
    auto a = bowed({ 1e6f, 2.0f, 10.0f, 10.0f, 5.0f });
    auto b = bowed({ kSR * 0.45f, BowedString::kMaxPosition, 1.0f, 1.0f, 1.0f });
    auto outA = capture(a, 4410);
    auto outB = capture(b, 4410);
    for (size_t i = 0; i < outA.size(); ++i)
        REQUIRE(outA[i] == outB[i]);
}

// --- Long-run stability -------------------------------------------------

TEST_CASE("BowedString: 10s stability under continuous bowing", "[bowed][stability]")
{
    // 10 s at heavy pressure and minimal damping (the slowest-decaying,
    // hardest-driven loop): finite, under the ceiling, and still sounding
    // in the last second (the oscillation neither dies nor runs away).
    auto s = bowed({ 220.0f, 0.3f, 0.0f, 0.9f, 0.7f });
    const int N = 10 * static_cast<int>(kSR);
    double lastSecond = 0.0;
    for (int n = 0; n < N; ++n)
    {
        const float v = s.process();
        REQUIRE(std::isfinite(v));
        REQUIRE(std::fabs(v) <= kOutputCeiling);
        if (n >= N - static_cast<int>(kSR)) lastSecond += double(v) * v;
    }
    // Audibility floor −40 dBFS rms: far above the 1e-25 anti-denormal
    // floor, far below a held bowed tone at bow speed 0.7.
    REQUIRE(std::sqrt(lastSecond / kSR) > 0.01);
}

// --- Extreme parameter combos ------------------------------------------

TEST_CASE("BowedString: extreme combos stay finite and bounded", "[bowed][extreme]")
{
    SECTION("max pressure × min damping × max bow × bridge-most position")
    {
        auto s = bowed({ 110.0f, BowedString::kMinPosition, 0.0f, 1.0f, 1.0f });
        auto out = capture(s, 88200);
        for (float v : out)
        {
            REQUIRE(std::isfinite(v));
            REQUIRE(std::fabs(v) <= kOutputCeiling);
        }
    }

    SECTION("zero pressure + full bow speed → silence")
    {
        // Pressure 0 → stick width 0 → the junction adds nothing.
        auto s = bowed({ 220.0f, 0.1f, 0.5f, 0.0f, 1.0f });
        auto out = capture(s, 4410);
        for (float v : out)
            REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));
    }

    SECTION("full pressure + bow speed 0 → silence")
    {
        // A friction law with no stick regime self-excites here.  Speed 0
        // → stick width 0 → no coupling, and a resting string stays at rest.
        auto s = bowed({ 220.0f, 0.1f, 0.5f, 1.0f, 0.0f });
        auto out = capture(s, 44100);
        for (float v : out)
            REQUIRE_THAT(v, WithinAbs(0.0f, 1e-10f));
    }

    SECTION("pitch at the Nyquist guard with every other extreme")
    {
        // sr × 0.45 = 19845 Hz → one period is 2.2 samples, the shortest
        // loop the model has to represent (both segments ≥ 1 sample).
        auto s = bowed({ 19845.0f, BowedString::kMaxPosition, 1.0f, 1.0f, 1.0f });
        auto out = capture(s, 44100);
        for (float v : out)
        {
            REQUIRE(std::isfinite(v));
            REQUIRE(std::fabs(v) <= kOutputCeiling);
        }
    }

    SECTION("parameter sweeps every sample")
    {
        // Pitch, position, damping, pressure and speed all moving at once
        // (the plugin layer calls the setters per sample under modulation).
        BowedString s;
        s.prepare(kSR);
        for (int n = 0; n < 4 * static_cast<int>(kSR); ++n)
        {
            const float t = static_cast<float>(n) / kSR;
            s.setFrequency(30.0f * std::pow(2.0f, 9.0f * (0.5f + 0.5f * std::sin(1.3f * t))));
            s.setPosition(0.02f + 0.43f * (0.5f + 0.5f * std::sin(0.7f * t)));
            s.setDamping(0.5f + 0.5f * std::sin(2.1f * t));
            s.setPressure(0.5f + 0.5f * std::sin(3.3f * t));
            s.setBowVelocity(std::sin(0.9f * t));
            const float v = s.process();
            REQUIRE(std::isfinite(v));
            REQUIRE(std::fabs(v) <= kOutputCeiling);
        }
    }
}
