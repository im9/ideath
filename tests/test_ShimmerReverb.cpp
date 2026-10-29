#include <catch2/catch_test_macros.hpp>
#include <ideath/HallReverb.h>
#include <ideath/ShimmerReverb.h>
#include "ReverbTestUtils.h"
#include <cmath>
#include <vector>

using namespace reverbtest;
using Mode = ideath::ShimmerReverb::Mode;

static constexpr float kSR = 48000.0f;

// Shared tolerances (derivations)
// -------------------------------
// kPitchTolCents = 10: pitch discrimination for sustained tones is
//   5–10 cents; a voice landing within 10 cents of the interval is in tune.
// kT60Tol = 10 %: twice the ≈ 5 % reverberation-time JND.
// kLevelTolDb = 1 dB: broadband level JND; a larger HALL ↔ SHIMMER step
//   is heard as a jump when a host crossfades between the two types.
static constexpr double kPitchTolCents = 10.0;
static constexpr double kT60Tol = 0.10;
static constexpr double kLevelTolDb = 1.0;

static float sine(double hz, size_t i, float amp = 0.5f)
{
    return amp * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / kSR));
}

// Single-voice shimmer: voice 0 at `semis`, voice 1 off, modulation off so
// the analysed spectrum is the shifter's alone.
static void oneVoice(ideath::ShimmerReverb& rev, float semis, Mode mode, float amount)
{
    rev.prepare(kSR);
    rev.setMix(1.0f);
    rev.setSize(0.3f);
    rev.setDamp(0.0f);
    rev.setModDepth(0.0f);
    rev.setMode(mode);
    rev.setShimmer(amount);
    rev.setVoicePitch(0, semis);
    rev.setVoiceLevel(0, 1.0f);
    rev.setVoiceLevel(1, 0.0f);
}

// Mono wet output for a steady sine of `seconds`.
static std::vector<float> renderSine(ideath::ShimmerReverb& rev, double hz, float seconds)
{
    std::vector<float> y(static_cast<size_t>(seconds * kSR));
    for (size_t i = 0; i < y.size(); ++i)
    {
        auto [l, r] = rev.process(sine(hz, i));
        y[i] = 0.5f * (l + r);
    }
    return y;
}

// Noise band around `hz`: white noise through the 2/3-octave band filter
// twice (8th order).  Content an octave below sits ≥ 40 dB down, so a +12
// voice cannot carry energy up into the measured band.  A noise burst
// rather than a sine: a sine burst excites only one or two modes, which
// beat under line modulation and scatter the T30 by tens of percent (see
// test_ReverbTank).
static std::vector<float> noiseBand(float hz, size_t n, uint32_t seed = 0x12345678u,
                                    float q = 2.0f)
{
    std::vector<float> x(n);
    White w;
    w.s = seed;
    for (auto& s : x)
        s = w.next();
    x = bandFilter(bandFilter(x, hz, kSR, q), hz, kSR, q);
    return x;
}

// ---------------------------------------------------------------------------
// Pitch
// ---------------------------------------------------------------------------

TEST_CASE("ShimmerReverb: each voice interval lands within 10 cents", "[shimmer]")
{
    // Input mode: one shift, no regeneration.  The strongest component
    // within ±100 cents of f·2^(n/12) must sit within kPitchTolCents.  The
    // last 2 s of a 3 s render are analysed (1 s for the tank to fill;
    // 2 s window → 0.5 Hz bins, < 2 cents at 440 Hz).
    const double f = 440.0;
    for (float semis : { 12.0f, 19.0f, 7.0f, 24.0f, -12.0f, -24.0f, 5.5f })
    {
        ideath::ShimmerReverb rev;
        oneVoice(rev, semis, Mode::Input, 1.0f);
        auto y = renderSine(rev, f, 3.0f);
        const double expect = f * std::pow(2.0, semis / 12.0);
        const double c = peakCents(y, static_cast<size_t>(kSR), y.size(), expect, 100.0, kSR);
        INFO("voice " << semis << " st: peak at " << c << " cents from " << expect << " Hz");
        REQUIRE(std::fabs(c) <= kPitchTolCents);
    }
}

TEST_CASE("ShimmerReverb: voice 1 shifts by its own interval", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    oneVoice(rev, 12.0f, Mode::Input, 1.0f);
    rev.setVoiceLevel(0, 0.0f);
    rev.setVoicePitch(1, 19.0f);
    rev.setVoiceLevel(1, 1.0f);
    auto y = renderSine(rev, 220.0, 3.0f);
    const double expect = 220.0 * std::pow(2.0, 19.0 / 12.0);
    const double c = peakCents(y, static_cast<size_t>(kSR), y.size(), expect, 100.0, kSR);
    INFO("voice 1 peak " << c << " cents from " << expect << " Hz");
    REQUIRE(std::fabs(c) <= kPitchTolCents);
}

TEST_CASE("ShimmerReverb: shimmer 0 puts no energy in the shifted band", "[shimmer]")
{
    // With amount 0 the voices contribute nothing and the tank is linear,
    // so the octave of a 220 Hz sine can only hold float rounding and
    // Hermite interpolation error (O(ω⁴) ≈ 10⁻⁷ at 220 Hz / 48 kHz,
    // −140 dB).  −80 dB is far above that floor and far below audibility.
    for (Mode mode : { Mode::Feedback, Mode::Input })
    {
        ideath::ShimmerReverb rev;
        oneVoice(rev, 12.0f, mode, 0.0f);
        rev.setModDepth(0.5f);
        auto y = renderSine(rev, 220.0, 3.0f);
        const size_t from = static_cast<size_t>(kSR);
        const double base = bandPower(y, from, y.size(), 220.0, 50.0, kSR);
        const double oct = bandPower(y, from, y.size(), 440.0, 50.0, kSR);
        INFO("octave band " << 10.0 * std::log10(oct / base) << " dB re fundamental");
        REQUIRE(oct <= base * 1e-8);
    }
}

TEST_CASE("ShimmerReverb: Input mode never stacks a second shift", "[shimmer]")
{
    // Input mode shifts once before the tank: a 220 Hz band → 440 Hz,
    // never 880 Hz.  Feedback mode at amount 1 puts the second generation
    // at ρ_max = −3 dB re the first.  A 30 dB gap is required in Input
    // mode: a partial 30 dB below an octave-adjacent partial lies under its
    // upward masking spread (≈ −25 dB at one octave for mid-level tones).
    // Stimulus: 220 Hz band noise (8th-order band-pass; its own content one
    // octave up is ≥ 40 dB down).  A single sine would land each
    // generation on one tank frequency, whose response swings ±10 dB from
    // mode peak to trough; a band averages over ~60 modes.
    auto ratio = [](Mode mode) {
        ideath::ShimmerReverb rev;
        oneVoice(rev, 12.0f, mode, 1.0f);
        rev.setModDepth(0.5f);
        const auto x = noiseBand(220.0f, static_cast<size_t>(4.0f * kSR));
        std::vector<float> y(x.size());
        for (size_t i = 0; i < y.size(); ++i)
        {
            auto [l, r] = rev.process(2.0f * x[i]);
            y[i] = 0.5f * (l + r);
        }
        const size_t from = static_cast<size_t>(2.0f * kSR);
        const double first = bandPower(y, from, y.size(), 440.0, 300.0, kSR);
        const double second = bandPower(y, from, y.size(), 880.0, 300.0, kSR);
        return 10.0 * std::log10(second / first);
    };
    const double input = ratio(Mode::Input);
    const double feedback = ratio(Mode::Feedback);
    INFO("880/440 Hz bands: Input " << input << " dB, Feedback " << feedback << " dB");
    REQUIRE(input <= -30.0);
    // The same measurement sees the second generation in Feedback mode:
    // nominal −3 dB; −10 dB leaves 7 dB for the anti-alias filter, the
    // voice's 200 ms power normaliser settling and the band edges.
    REQUIRE(feedback >= -10.0);
}

// ---------------------------------------------------------------------------
// Decay independence
// ---------------------------------------------------------------------------

static double unshiftedT60(Mode mode, float amount)
{
    // 0.5 s band-noise burst at 220 Hz; T60 of the 220 Hz band, averaged
    // over seeds.  A +12 voice moves energy up only, so nothing re-enters
    // the 220 Hz band.
    return seedAverage([&](uint32_t seed) {
        ideath::ShimmerReverb rev;
        oneVoice(rev, 12.0f, mode, amount);
        rev.setSize(0.5f);
        rev.setModDepth(0.5f);
        const size_t burst = static_cast<size_t>(0.5f * kSR);
        const auto x = noiseBand(220.0f, burst, seed);
        const double t60 = ideath::HallReverb::sizeToT60(0.5f);
        std::vector<float> y(burst + tailSamples(t60, kSR));
        for (size_t i = 0; i < y.size(); ++i)
        {
            auto [l, r] = rev.process(i < burst ? 2.0f * x[i] : 0.0f);
            y[i] = 0.5f * (l + r);
        }
        return t30(bandFilter(y, 220.0f, kSR), burst, kSR);
    });
}

TEST_CASE("ShimmerReverb: shimmer amount leaves the unshifted T60 unchanged", "[shimmer]")
{
    for (Mode mode : { Mode::Feedback, Mode::Input })
    {
        const double ref = unshiftedT60(mode, 0.0f);
        REQUIRE(ref > 0.0);
        for (float amount : { 0.5f, 1.0f })
        {
            const double t = unshiftedT60(mode, amount);
            INFO("mode " << (mode == Mode::Feedback ? "Feedback" : "Input")
                 << " amount " << amount << ": " << t << " s vs " << ref << " s");
            REQUIRE(std::fabs(t / ref - 1.0) <= kT60Tol);
        }
    }
}

TEST_CASE("ShimmerReverb: size sets the same low-frequency T60 as HallReverb", "[shimmer]")
{
    // Both types read setSize through HallReverb::sizeToT60 = 0.5·60^size.
    // Shimmer 0 here: the independence from the amount is its own test.
    for (float size : { 0.0f, 0.5f, 1.0f })
    {
        ideath::ShimmerReverb rev;
        oneVoice(rev, 12.0f, Mode::Feedback, 0.0f);
        rev.setSize(size);
        rev.setModDepth(0.5f);
        const double t60 = ideath::HallReverb::sizeToT60(size);
        const double m = seedAverage([&](uint32_t seed) {
            rev.reset();
            const size_t burst = static_cast<size_t>(0.5f * kSR);
            std::vector<float> y(burst + tailSamples(t60, kSR));
            White w;
            w.s = seed;
            for (size_t i = 0; i < y.size(); ++i)
            {
                auto [l, r] = rev.process(i < burst ? 0.5f * w.next() : 0.0f);
                y[i] = 0.5f * (l + r);
            }
            return t30(bandFilter(y, 250.0f, kSR), burst, kSR);
        });
        INFO("size " << size << ": " << m << " s, nominal " << t60 << " s");
        REQUIRE(std::fabs(m / t60 - 1.0) <= kT60Tol);
    }
}

// ---------------------------------------------------------------------------
// Freeze
// ---------------------------------------------------------------------------

TEST_CASE("ShimmerReverb: freeze holds for 10 s without growth", "[shimmer]")
{
    // Feedback mode, both voices, full amount — the case that regenerates
    // hardest.  1 dB growth bound = level JND; 3 dB fall = half power.
    ideath::ShimmerReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    rev.setSize(0.6f);
    rev.setShimmer(1.0f);
    rev.setVoiceLevel(1, 1.0f);
    Pink p;
    for (int i = 0; i < static_cast<int>(2.0f * kSR); ++i)
        rev.process(0.5f * p.next());
    rev.setFreeze(true);
    double first = 0.0;
    for (int b = 0; b < 10; ++b)
    {
        double e = 0.0;
        for (int i = 0; i < static_cast<int>(kSR); ++i)
        {
            // Input keeps arriving during freeze; it must not enter.
            auto [l, r] = rev.process(0.5f * p.next());
            REQUIRE(std::isfinite(l));
            e += static_cast<double>(l) * l + static_cast<double>(r) * r;
        }
        const double level = std::sqrt(e / kSR);
        if (b == 0)
            first = level;
        INFO("block " << b << ": " << db(level / first) << " dB");
        REQUIRE(db(level / first) <= 1.0);
        REQUIRE(db(level / first) >= -3.0);
    }
}

TEST_CASE("ShimmerReverb: engaging freeze does not step the level", "[shimmer]")
{
    // Feedback mode, full shimmer, heavy damping (where the level
    // normalisation depends on damping).  The wet RMS over the 0.2 s before
    // and after setFreeze(true) must agree within the 1 dB level JND.
    ideath::ShimmerReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    rev.setSize(0.6f);
    rev.setDamp(1.0f);
    rev.setShimmer(1.0f);
    Pink p;
    for (int i = 0; i < static_cast<int>(3.0f * kSR); ++i)
        rev.process(0.5f * p.next());
    const int win = static_cast<int>(0.2f * kSR);
    double before = 0.0, after = 0.0;
    for (int i = 0; i < win; ++i)
    {
        auto [l, r] = rev.process(0.0f); // tail only, same input either side
        before += static_cast<double>(l) * l + static_cast<double>(r) * r;
    }
    rev.setFreeze(true);
    for (int i = 0; i < win; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        after += static_cast<double>(l) * l + static_cast<double>(r) * r;
    }
    const double stepDb = 10.0 * std::log10(after / before);
    INFO("level step at freeze: " << stepDb << " dB");
    // A free tail at size 0.6 (T60 5.8 s) falls 60·0.2/5.8 = 2.1 dB per
    // 0.2 s window.  The "before" window's mean sits half of that above
    // the level at the freeze instant, which the frozen tank then holds:
    // a step-free freeze reads −1.05 dB.  Allowed: ±1 dB (level JND)
    // around it.  (Forcing λ = 1 while frozen measured −2.36 dB.)
    REQUIRE(stepDb >= -2.05);
    REQUIRE(stepDb <= -0.05);
}

TEST_CASE("ShimmerReverb: frozen tail never climbs in pitch", "[shimmer]")
{
    // Before freeze a 220 Hz band regenerates 440, 880, 1760, 3520 Hz in
    // Feedback mode.  While frozen no new shifted sound enters, so the
    // energy in each generation band — and above the highest one — must
    // not grow between the first and the tenth frozen second (1 dB JND).
    // Band-noise stimulus (a single sine puts one or two modes in a band,
    // whose beating swung it ±5 dB).  Even with band noise a single burst's
    // band moves both ways with σ ≈ 0.8 dB (measured, 6 seeds: −3.2 …
    // +1.3 dB, no trend), so the change is the mean of 6 seeds
    // (σ/√6 ≈ 0.33 dB).
    for (double hz : { 440.0, 880.0, 1760.0, 3520.0, 7040.0 })
    {
        const double change = seedAverage([&](uint32_t seed) {
            ideath::ShimmerReverb rev;
            oneVoice(rev, 12.0f, Mode::Feedback, 1.0f);
            rev.setSize(0.7f);
            rev.setModDepth(0.5f);
            const auto x = noiseBand(220.0f, static_cast<size_t>(1.5f * kSR), seed);
            for (size_t i = 0; i < x.size(); ++i)
                rev.process(2.0f * x[i]);
            rev.setFreeze(true);
            std::vector<float> y(static_cast<size_t>(10.0f * kSR));
            for (size_t i = 0; i < y.size(); ++i)
            {
                auto [l, r] = rev.process(0.0f);
                y[i] = 0.5f * (l + r);
            }
            const size_t sec = static_cast<size_t>(kSR);
            return 10.0 * std::log10(bandPower(y, y.size() - sec, y.size(), hz, 300.0, kSR)
                                     / bandPower(y, 0, sec, hz, 300.0, kSR));
        }, 6);
        INFO(hz << " Hz band: " << change << " dB over 9 s (mean of 6 seeds)");
        REQUIRE(change <= 1.0);
    }
}

// ---------------------------------------------------------------------------
// Level match with HallReverb
// ---------------------------------------------------------------------------

template <typename Process>
static double wetRms(Process&& process, float seconds, float from)
{
    Pink p;
    double e = 0.0;
    size_t n = 0;
    for (size_t i = 0; i < static_cast<size_t>(seconds * kSR); ++i)
    {
        auto [l, r] = process(0.5f * p.next());
        if (i >= static_cast<size_t>(from * kSR))
        {
            e += static_cast<double>(l) * l + static_cast<double>(r) * r;
            ++n;
        }
    }
    return std::sqrt(e / static_cast<double>(n));
}

TEST_CASE("ShimmerReverb: wet level matches HallReverb within 1 dB on pink noise",
          "[shimmer]")
{
    // Sizes 0.3 / 0.8 × damp 0 / 0.3 / 1: damping changes how much energy
    // each shifted generation brings back, so it is part of the matrix.
    for (float size : { 0.3f, 0.8f })
    for (float damp : { 0.0f, 0.3f, 1.0f })
    {
        const float t60 = ideath::HallReverb::sizeToT60(size);
        // Steady state after 0.6·T60 (energy residual 10^(−3.6)); then
        // 4 s of measurement.
        const float from = 0.6f * t60 + 0.5f;
        const float seconds = from + 4.0f;

        ideath::HallReverb hall;
        hall.prepare(kSR);
        hall.setMix(1.0f);
        hall.setSize(size);
        hall.setDamp(damp);
        const double hallRms = wetRms([&](float x) { return hall.process(x); }, seconds, from);

        for (Mode mode : { Mode::Feedback, Mode::Input })
            for (float amount : { 0.0f, 0.5f, 1.0f })
                for (float level1 : { 0.0f, 1.0f })
                {
                    ideath::ShimmerReverb shim;
                    shim.prepare(kSR);
                    shim.setMix(1.0f);
                    shim.setSize(size);
                    shim.setDamp(damp);
                    shim.setMode(mode);
                    shim.setShimmer(amount);
                    shim.setVoiceLevel(1, level1);
                    const double s = wetRms([&](float x) { return shim.process(x); }, seconds, from);
                    INFO("size " << size << " damp " << damp << " mode " << (mode == Mode::Feedback ? "Feedback" : "Input")
                         << " amount " << amount << " voice1 " << level1 << ": "
                         << db(s / hallRms) << " dB re Hall");
                    REQUIRE(std::fabs(db(s / hallRms)) <= kLevelTolDb);
                }
    }
}

TEST_CASE("ShimmerReverb: Input-mode shift level does not depend on note length",
          "[shimmer]")
{
    // Input mode, amount 1: the shifted copy carries the input's energy
    // (ρ_max = 1) for any note length.  Band-noise bursts of 50 ms … 3 s
    // at +12, +24 (220 Hz source) and −12, −24 (1760 Hz source, so the
    // shifted band, centred at 880 / 440 Hz, sits above the 120 Hz low cut).
    //
    // The shifted copy then rings through the tank in a different band from
    // the source, and the tank's band-to-band energy gain is not flat at
    // this size (measured, 50 ms bursts: −5.7 dB at 220 Hz vs −3.9 dB at
    // 880 Hz — modal colouration, fixed per band, not seed noise).  So each
    // case is compared with a control: the same tank, shimmer 0, fed the
    // same-length burst directly in the shifted band.  Grain shifting keeps
    // durations, so the shifted copy should match the control:
    //   (shifted / source)_shimmer − (shifted band / source band)_control = 0 dB.
    // A short note's copies are weighted by the grain windows at the
    // moments the grains cross it, so 12 seeds start at onsets spread over
    // one grain period after a 1 s warm-up, and energies are summed.
    // Tolerance ±1.5 dB: the 1 dB level JND plus 0.5 dB for the downward
    // voices' 120 Hz low cut (the lower edge of the −24 band, 220 Hz, is
    // −0.36 dB down on the 2nd-order Butterworth, rounded up).
    const int seeds = 12;
    // Two-octave bands (Q 0.7): a 2/3-octave band holds few enough tank
    // modes that a short burst's level depends on how its fine spectrum
    // lines up with them (±2 dB between two bursts of the same band
    // energy); a two-octave band averages over ≈ 3× as many.
    const float kWideQ = 0.7f;
    // Wet-output energy in band `measureHz` per unit of input energy, for a
    // `bandHz` noise burst.
    auto render = [&](float semis, float amount, float bandHz, float burstSec, int k,
                      float measureHz) {
        ideath::ShimmerReverb rev;
        oneVoice(rev, semis, Mode::Input, amount);
        rev.setModDepth(0.5f);
        const size_t onset = static_cast<size_t>(kSR)
                           + static_cast<size_t>(k) * static_cast<size_t>(0.1f * kSR) / seeds;
        const size_t n = static_cast<size_t>(burstSec * kSR);
        const auto x = noiseBand(bandHz, n, 0x9E3779B9u * static_cast<uint32_t>(k + 1), kWideQ);
        std::vector<float> y(onset + n + static_cast<size_t>(4.0f * kSR));
        double eIn = 0.0;
        for (size_t i = 0; i < y.size(); ++i)
        {
            const float in = (i >= onset && i < onset + n) ? 2.0f * x[i - onset] : 0.0f;
            eIn += static_cast<double>(in) * in;
            auto [l, r] = rev.process(in);
            y[i] = 0.5f * (l + r);
        }
        const auto band = bandFilter(bandFilter(y, measureHz, kSR, kWideQ), measureHz, kSR, kWideQ);
        double e = 0.0;
        for (float v : band)
            e += static_cast<double>(v) * v;
        return e / eIn;
    };
    for (float semis : { 12.0f, 24.0f, -12.0f, -24.0f })
        for (float burstSec : { 0.05f, 0.15f, 0.4f, 3.0f })
        {
            const float sourceHz = semis > 0.0f ? 220.0f : 1760.0f;
            const float shiftedHz = sourceHz * std::pow(2.0f, semis / 12.0f);
            double shimSrc = 0.0, shimShift = 0.0, ctlSrc = 0.0, ctlShift = 0.0;
            for (int k = 0; k < seeds; ++k)
            {
                shimSrc += render(semis, 1.0f, sourceHz, burstSec, k, sourceHz);
                shimShift += render(semis, 1.0f, sourceHz, burstSec, k, shiftedHz);
                ctlSrc += render(semis, 0.0f, sourceHz, burstSec, k, sourceHz);
                ctlShift += render(semis, 0.0f, shiftedHz, burstSec, k, shiftedHz);
            }
            const double shim = 10.0 * std::log10(shimShift / shimSrc);
            const double ctl = 10.0 * std::log10(ctlShift / ctlSrc);
            INFO(semis << " st, " << burstSec << " s burst: shifted/source " << shim
                 << " dB, tank control " << ctl << " dB, difference " << shim - ctl << " dB");
            REQUIRE(std::fabs(shim - ctl) <= 1.5);
        }
}

// ---------------------------------------------------------------------------
// Bounds, state, safety
// ---------------------------------------------------------------------------

TEST_CASE("ShimmerReverb: extreme settings stay within kMaxOutput", "[shimmer][stability]")
{
    // Size 1, damp 0, modulation 0 (sharpest modes), full shimmer, both
    // voices, both modes; 10 s each of full-scale white noise and of a
    // full-scale sine at a few frequencies.  Output must stay finite and
    // within the header ceiling.
    for (Mode mode : { Mode::Feedback, Mode::Input })
        for (int stim = 0; stim < 4; ++stim)
        {
            ideath::ShimmerReverb rev;
            rev.prepare(kSR);
            rev.setMix(1.0f);
            rev.setSize(1.0f);
            rev.setDamp(0.0f);
            rev.setModDepth(0.0f);
            rev.setShimmer(1.0f);
            rev.setMode(mode);
            rev.setVoicePitch(0, 12.0f);
            rev.setVoicePitch(1, -12.0f);
            rev.setVoiceLevel(1, 1.0f);
            White w;
            const double hz[] = { 0.0, 110.0, 523.25, 2000.0 };
            float peak = 0.0f;
            for (size_t i = 0; i < static_cast<size_t>(10.0f * kSR); ++i)
            {
                const float x = stim == 0 ? w.next() : sine(hz[stim], i, 1.0f);
                auto [l, r] = rev.process(x);
                REQUIRE(std::isfinite(l));
                REQUIRE(std::isfinite(r));
                peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            }
            INFO("mode " << static_cast<int>(mode) << " stimulus " << stim << " peak " << peak);
            REQUIRE(peak <= ideath::ShimmerReverb::kMaxOutput);
        }
}

TEST_CASE("ShimmerReverb: silence in produces silence out", "[shimmer]")
{
    // Anti-denormal offsets (1e-25) are the only possible residue.
    ideath::ShimmerReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    for (int i = 0; i < 48000; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 1e-20f);
        REQUIRE(std::fabs(r) < 1e-20f);
    }
}

TEST_CASE("ShimmerReverb: reset matches a freshly prepared instance", "[shimmer]")
{
    auto configure = [](ideath::ShimmerReverb& r) {
        r.prepare(kSR);
        r.setMix(1.0f);
        r.setShimmer(0.8f);
        r.setVoiceLevel(1, 0.7f);
        r.setPreDelay(0.03f);
    };
    ideath::ShimmerReverb a, b;
    configure(a);
    configure(b);
    White w;
    for (int i = 0; i < 30000; ++i)
        a.process(w.next());
    a.reset();
    for (int i = 0; i < 30000; ++i)
    {
        const float x = i < 100 ? 0.5f : 0.0f;
        auto [al, ar] = a.process(x);
        auto [bl, br] = b.process(x);
        REQUIRE(al == bl);
        REQUIRE(ar == br);
    }
}

TEST_CASE("ShimmerReverb: reset returns to silence", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
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

TEST_CASE("ShimmerReverb: silent tail never produces subnormals", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    rev.setSize(0.0f);
    rev.setDamp(1.0f);
    rev.setVoicePitch(1, -12.0f);
    rev.setVoiceLevel(1, 1.0f);
    rev.process(1.0f);
    for (int i = 0; i < static_cast<int>(10.0f * kSR); ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fpclassify(l) != FP_SUBNORMAL);
        REQUIRE(std::fpclassify(r) != FP_SUBNORMAL);
    }
}

TEST_CASE("ShimmerReverb: pre-delay holds the tail back", "[shimmer]")
{
    // Nothing may come out before the pre-delay (impulse at 0, 100 ms
    // pre-delay); the tail must start within the first 50 ms after it
    // (the input diffuser's span is ≈ 30 ms at 48 kHz).
    ideath::ShimmerReverb rev;
    rev.prepare(kSR);
    rev.setMix(1.0f);
    rev.setPreDelay(0.1f);
    const int pre = static_cast<int>(0.1f * kSR);
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

TEST_CASE("ShimmerReverb: dry/wet mix 0 passes the input bit-exact", "[shimmer]")
{
    ideath::ShimmerReverb rev;
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

TEST_CASE("ShimmerReverb: modulation depth moves the shifted voices", "[shimmer]")
{
    // MOD detunes the voices by up to ±kMaxDetuneCents.  With the tank
    // modulation at its deepest the shifted partial's peak must wander
    // off the exact interval at some point of a 4 s render, while at
    // depth 0 it never does.  Peak tracked over 0.5 s windows (2 Hz bins
    // = 4 cents at 880 Hz, finer than the 8-cent swing).
    auto maxDrift = [](float depth) {
        ideath::ShimmerReverb rev;
        oneVoice(rev, 12.0f, Mode::Input, 1.0f);
        rev.setModDepth(depth);
        rev.setModRate(0.5f);
        rev.setSize(0.0f);
        auto y = renderSine(rev, 440.0, 5.0f);
        double drift = 0.0;
        const size_t win = static_cast<size_t>(0.5f * kSR);
        for (size_t from = static_cast<size_t>(kSR); from + win <= y.size(); from += win)
            drift = std::max(drift, std::fabs(peakCents(y, from, from + win, 880.0, 30.0, kSR)));
        return drift;
    };
    const double still = maxDrift(0.0f);
    const double moving = maxDrift(1.0f);
    INFO("max drift: depth 0 " << still << " cents, depth 1 " << moving << " cents");
    // Half the 8-cent swing is the smallest drift the 4-cent bins resolve.
    REQUIRE(moving >= 4.0);
    REQUIRE(moving > still);
}

TEST_CASE("ShimmerReverb: parameter clamping", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    rev.prepare(kSR);
    rev.setSize(5.0f);
    rev.setDamp(-1.0f);
    rev.setShimmer(3.0f);
    rev.setMix(2.0f);
    rev.setPreDelay(5.0f);
    rev.setModDepth(9.0f);
    rev.setModRate(1000.0f);
    rev.setVoicePitch(0, 99.0f);
    rev.setVoicePitch(1, -99.0f);
    rev.setVoiceLevel(0, 7.0f);
    rev.setVoiceLevel(1, -7.0f);
    rev.setVoicePitch(5, 3.0f); // out-of-range voice index is ignored
    rev.setVoiceLevel(-1, 3.0f);
    White w;
    for (int i = 0; i < 48000; ++i)
    {
        auto [l, r] = rev.process(w.next());
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        REQUIRE(std::fabs(l) <= ideath::ShimmerReverb::kMaxOutput);
    }
}

TEST_CASE("ShimmerReverb: default-constructible", "[shimmer]")
{
    ideath::ShimmerReverb rev;
    auto [l, r] = rev.process(1.0f);
    REQUIRE(std::isfinite(l));
    REQUIRE(std::isfinite(r));
}
