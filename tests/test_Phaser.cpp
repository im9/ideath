#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Phaser.h>
#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

namespace {

struct Xorshift
{
    uint32_t s;
    explicit Xorshift(uint32_t seed) : s(seed) {}
    float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(static_cast<int32_t>(s)) / 2.147483648e9f;
    }
};

// Short-time RMS of a signal in a sliding window.
std::vector<float> shortTimeRms(const std::vector<float>& x, int window)
{
    const int N = static_cast<int>(x.size());
    std::vector<float> out(static_cast<size_t>(N), 0.0f);
    double runningSum = 0.0;
    // Initial fill.
    for (int i = 0; i < window && i < N; ++i)
        runningSum += static_cast<double>(x[static_cast<size_t>(i)]) * x[static_cast<size_t>(i)];
    for (int i = 0; i < N; ++i)
    {
        if (i >= window)
        {
            const float leaving = x[static_cast<size_t>(i - window)];
            runningSum -= static_cast<double>(leaving) * leaving;
        }
        if (i + window - 1 >= window && i > 0)
        {
            const int addIdx = i + window - 1;
            if (addIdx < N)
                runningSum += static_cast<double>(x[static_cast<size_t>(addIdx)]) * x[static_cast<size_t>(addIdx)];
        }
        out[static_cast<size_t>(i)] = static_cast<float>(std::sqrt(std::max(0.0, runningSum / static_cast<double>(window))));
    }
    return out;
}

} // namespace

// --- Bypass sanity ---

TEST_CASE("Phaser: mix=0 is exact bypass", "[phaser]")
{
    // With mix = 0 the wet cascade is fully attenuated — output must equal
    // input to float rounding regardless of stage / feedback / sweep state.
    ideath::Phaser ph;
    ph.prepare(kSampleRate);
    ph.setStages(4);
    ph.setRate(2.0f);
    ph.setDepth(0.8f);
    ph.setCenterHz(1000.0f);
    ph.setFeedback(0.5f);
    ph.setMix(0.0f);

    Xorshift rng(0xABCDu);
    for (int i = 0; i < 2048; ++i)
    {
        const float in = rng.next();
        const float out = ph.process(in);
        REQUIRE_THAT(out, WithinAbs(in, 1e-6f));
    }
}

TEST_CASE("Phaser: stages requested = 0 clamps to minStages", "[phaser]")
{
    // Spec: setStages(0) must clamp up to kMinStages (2).  The primitive
    // must still process cleanly under this input.
    ideath::Phaser ph;
    ph.prepare(kSampleRate);
    ph.setStages(0);
    REQUIRE(ph.getStages() >= ideath::Phaser::kMinStages);

    ph.setStages(-100);
    REQUIRE(ph.getStages() >= ideath::Phaser::kMinStages);

    ph.setStages(1000);
    REQUIRE(ph.getStages() <= ideath::Phaser::kMaxStages);
}

// --- Cascade produces a notch (static config) ---

TEST_CASE("Phaser: static config attenuates near the notch frequency", "[phaser]")
{
    // With rate → very slow (effectively static within the observation
    // window) and depth = 0, the phaser is a static frequency response.
    // A 4-stage cascade centred at 1 kHz has its notch pair somewhere near
    // 1 kHz (each stage contributes cumulative phase, and where cumulative
    // = ±π the sum with dry drops to |dry − wet|).
    //
    // Test strategy: hold the cascade centre fixed at 1 kHz, then probe two
    // test frequencies — one at the centre (expected notch attenuation)
    // and one two decades below (expected passthrough: all stages act as
    // near-zero-phase all-pass at ω << ω_c, cumulative phase ≈ 0 →
    // wet ≈ dry → sum = dry).  Compare the resulting steady-state sine RMS
    // ratios: near-notch must be measurably below far-from-notch.
    auto steadyRms = [&](float freqHz, float centerHz)
    {
        ideath::Phaser ph;
        ph.prepare(kSampleRate);
        ph.setStages(4);
        ph.setRate(0.01f);   // effectively static within our observation window
        ph.setDepth(0.0f);
        ph.setCenterHz(centerHz);
        ph.setFeedback(0.0f);
        ph.setMix(0.5f);

        constexpr int totalN  = 8192;
        constexpr int warmupN = 2048;   // let phaser & sine reach steady state
        double sumSq = 0.0;
        for (int i = 0; i < totalN; ++i)
        {
            const float in = std::sin(6.28318530717958f * freqHz * static_cast<float>(i) / kSampleRate);
            const float out = ph.process(in);
            if (i >= warmupN)
                sumSq += static_cast<double>(out) * out;
        }
        return static_cast<float>(std::sqrt(sumSq / static_cast<double>(totalN - warmupN)));
    };

    // First-order allpass phase runs from 0 (DC) to -π (Nyquist), passing
    // -π/2 at the break frequency ω_c.  For a 4-stage cascade the sum of
    // dry + wet forms a notch where cumulative phase = -π, i.e. per-stage
    // phase = -π/4.  For a bilinear-warped first-order allpass this happens
    // at ω = ω_c · tan(π/8) ≈ 0.414·ω_c (notch below the centre) and at
    // ω_c · tan(3π/8) ≈ 2.414·ω_c (notch above).  AT the centre itself
    // cumulative phase is -2π (=0), so wet is *in phase* with dry — that's
    // a peak, not a notch.
    //
    // Test:
    //   50 Hz  (well below centre, per-stage phase ≈ 0 → cumulative ≈ 0 →
    //           wet ≈ dry → sum RMS = sine RMS ≈ 0.7071) — the "passthrough"
    //           baseline.
    //   414 Hz (0.414 · 1 kHz — the theoretical notch below centre) — should
    //           be measurably lower than the baseline.
    const float rmsFar   = steadyRms(50.0f,  1000.0f);
    const float rmsNotch = steadyRms(414.0f, 1000.0f);

    // Notch: notch-freq RMS should be *below* passthrough by a measurable
    // margin.  Threshold 30 %: idealised cascade drops the sum to 0 at the
    // notch, but the digital-allpass frequency warp and finite Q soften
    // this in practice.  Any working implementation should achieve at
    // least a 30 % drop; a broken cascade (e.g. all stages identical
    // sign-flip, no phase behavior across frequency) would return ratios
    // near 1.
    REQUIRE(rmsNotch < rmsFar * 0.7f);

    // Sanity: rmsFar itself should be close to the sine RMS 0.7071 (the
    // cascade is unity-magnitude at low freq → dry + wet ≈ 2·dry → 0.5·sum
    // = dry → RMS = 0.7071).  ±10 % window.
    REQUIRE_THAT(rmsFar, WithinAbs(0.7071f, 0.7071f * 0.1f));
}

// --- Dynamic sweep produces amplitude modulation at the LFO rate ---

TEST_CASE("Phaser: notch sweep amplitude-modulates white noise at the LFO rate", "[phaser]")
{
    // Feed white noise through a moderately-swept phaser (rate 5 Hz).
    // As the notch sweeps up/down through the noise spectrum the wet-vs-dry
    // interference pattern makes the sum's short-time RMS drop each time a
    // deep notch passes through the loudest part of the spectrum.
    //
    // For a sine LFO the notch traverses the range once per cycle (up + down)
    // → we expect ≈ 2 attenuation events per LFO period → 2·rate zero-slope
    // dips per second → ≈ 10 Hz envelope modulation for a 5 Hz LFO.
    //
    // A robust, threshold-tolerant test: count local minima of the
    // envelope over the observation window and check that the count matches
    // the expected modulation cycle count within a generous window.
    ideath::Phaser ph;
    ph.prepare(kSampleRate);
    ph.setStages(6);          // deeper notches
    ph.setRate(5.0f);
    ph.setDepth(0.9f);        // wide sweep
    ph.setCenterHz(800.0f);
    ph.setFeedback(0.5f);
    ph.setMix(0.5f);

    Xorshift rng(0x1234u);
    // Warm 200 ms.
    for (int i = 0; i < static_cast<int>(0.2f * kSampleRate); ++i)
        (void)ph.process(rng.next());

    // 2 s observation → 10 LFO cycles → 20 envelope dips expected.
    const int N = static_cast<int>(2.0f * kSampleRate);
    std::vector<float> y(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        y[static_cast<size_t>(i)] = ph.process(rng.next());

    // RMS window: 20 ms (882 samples @ 44.1 kHz) — short enough to resolve
    // 10 Hz envelope changes, long enough to smooth out per-sample noise.
    const int windowN = static_cast<int>(0.02f * kSampleRate);
    auto env = shortTimeRms(y, windowN);

    // Compute a threshold as (mean env − 0.5 × stddev).  Local minima below
    // that threshold, separated by at least (kSampleRate / (2 · rate · 2))
    // samples, are counted as distinct dip events.  Separation floor prevents
    // per-sample jitter from producing spurious counts.
    double sum = 0.0;
    for (float v : env) sum += v;
    const double mean = sum / static_cast<double>(env.size());
    double var = 0.0;
    for (float v : env) { const double d = v - mean; var += d * d; }
    const double stddev = std::sqrt(var / static_cast<double>(env.size()));
    const float threshold = static_cast<float>(mean - 0.5 * stddev);

    // Minimum inter-dip spacing: half of expected inter-dip period.  Rate 5 Hz
    // → 20 dips / 2 s → 100 ms between dips → min separation 50 ms = 2205 samp.
    const int minSep = 2205;

    int dipCount = 0;
    int lastDipIdx = -minSep;
    for (int i = windowN; i < N - windowN; ++i)
    {
        if (env[static_cast<size_t>(i)] < threshold &&
            i - lastDipIdx >= minSep)
        {
            ++dipCount;
            lastDipIdx = i;
        }
    }

    // Expected ≈ 20 dips over 2 s.  Allow generous ±50 % tolerance
    // ([10, 30]) — noise input's randomness + threshold placement make
    // exact counts unreliable but the order-of-magnitude match confirms the
    // sweep is happening at the LFO rate rather than static or absent.
    REQUIRE(dipCount >= 10);
    REQUIRE(dipCount <= 30);
}

// --- Feedback stability ---

TEST_CASE("Phaser: silent-input feedback loop stays bounded (60 s)", "[phaser]")
{
    // fb near the clamp, silent input.  Denormal guard should keep the
    // internal state from drifting to non-finite; peak must remain within
    // the ±1.0 documented ceiling.
    //
    // We prime with 1 s of noise so state is non-zero, then sustain silence
    // for 60 s and monitor peak / finiteness.
    ideath::Phaser ph;
    ph.prepare(kSampleRate);
    ph.setStages(6);
    ph.setRate(2.0f);
    ph.setDepth(0.9f);
    ph.setCenterHz(1000.0f);
    ph.setFeedback(0.9f);
    ph.setMix(1.0f);

    Xorshift rng(0xBEEF3u);
    for (int i = 0; i < static_cast<int>(kSampleRate); ++i)
        (void)ph.process(rng.next());

    float peak = 0.0f;
    const int sustainN = static_cast<int>(60.0f * kSampleRate);
    for (int i = 0; i < sustainN; ++i)
    {
        const float s = ph.process(0.0f);
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::fabs(s));
    }

    // Ceiling: documented ±1.0 for stable settings (allpass is unit gain,
    // mix ≤ 1, feedback < 1 within clamp).  Silent-input decay should stay
    // well inside that.
    REQUIRE(peak < 1.0f);
}

// --- Parameter clamping ---

TEST_CASE("Phaser: setters accept extreme values without producing NaN", "[phaser]")
{
    ideath::Phaser ph;
    ph.prepare(kSampleRate);

    ph.setRate(-100.0f);
    ph.setRate(1e6f);
    ph.setDepth(-5.0f);
    ph.setDepth(1000.0f);
    ph.setCenterHz(-100.0f);
    ph.setCenterHz(1e6f);
    ph.setFeedback(-10.0f);
    ph.setFeedback(10.0f);
    ph.setMix(-5.0f);
    ph.setMix(5.0f);
    ph.setStages(-1);
    ph.setStages(1000);

    for (int i = 0; i < 4096; ++i)
    {
        const float in = static_cast<float>((i % 100) - 50) * 0.02f;
        const float out = ph.process(in);
        REQUIRE(std::isfinite(out));
    }
}

// --- Reset ---

TEST_CASE("Phaser: reset clears internal state", "[phaser]")
{
    ideath::Phaser ph;
    ph.prepare(kSampleRate);
    ph.setStages(4);
    ph.setRate(2.0f);
    ph.setDepth(0.5f);
    ph.setCenterHz(1000.0f);
    ph.setFeedback(0.7f);
    ph.setMix(1.0f);

    for (int i = 0; i < 2000; ++i)
        (void)ph.process(0.5f);

    ph.reset();

    // After reset, silence in must give silence out (mix = 1 so no dry
    // bleed; wet stage state fully zeroed).  Exact identity to float
    // rounding.
    for (int i = 0; i < 200; ++i)
    {
        const float out = ph.process(0.0f);
        REQUIRE_THAT(out, WithinAbs(0.0f, 1e-6f));
    }
}
