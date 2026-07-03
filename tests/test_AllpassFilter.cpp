#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/AllpassFilter.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

static float rms(const std::vector<float>& buf, int begin = 0, int end = -1)
{
    if (end < 0) end = static_cast<int>(buf.size());
    double sum = 0.0;
    for (int i = begin; i < end; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(end - begin)));
}

// --- Structural: impulse response shape (Schroeder direct-form II) ---

TEST_CASE("AllpassFilter: impulse response first tap equals -g", "[allpass]")
{
    // Schroeder allpass: H(z) = (-g + z^-N) / (1 - g z^-N).
    // Direct-form II implementation:
    //     w[n] = x[n] + g w[n-N]
    //     y[n] = -g w[n] + w[n-N]
    // For an impulse x[0] = 1 (all other zero) and empty state:
    //     w[0] = 1 + g·0 = 1; y[0] = -g·1 + 0 = -g.
    ideath::AllpassFilter ap;
    ap.prepare(kSampleRate, 0.05f);
    ap.setDelay(0.005f); // 220 samples @ 44.1 kHz
    ap.setFeedback(0.7f);

    const float y0 = ap.process(1.0f);
    // Exact algebraic identity, so tolerance is float rounding only (~1e-6).
    REQUIRE_THAT(y0, WithinAbs(-0.7f, 1e-6f));
}

TEST_CASE("AllpassFilter: impulse response tap at N samples equals 1 - g^2", "[allpass]")
{
    // Continuing the derivation:
    //     Samples 1..N-1: input=0, w_delayed=0 → w=0, y=0.
    //     Sample N: w_delayed = w[0] = 1 → w[N] = 0 + g·1 = g;
    //               y[N] = -g·g + 1 = 1 - g².
    ideath::AllpassFilter ap;
    ap.prepare(kSampleRate, 0.05f);
    const float delaySec = 0.005f;
    const int   N        = static_cast<int>(delaySec * kSampleRate); // 220
    ap.setDelay(delaySec);
    const float g = 0.7f;
    ap.setFeedback(g);

    ap.process(1.0f);
    float yN = 0.0f;
    for (int i = 1; i <= N; ++i)
        yN = ap.process(0.0f);

    // 1 - 0.49 = 0.51. Identity, so tolerance is float rounding only.
    REQUIRE_THAT(yN, WithinAbs(1.0f - g * g, 1e-6f));
}

TEST_CASE("AllpassFilter: impulse response is energy-preserving (Parseval)", "[allpass]")
{
    // |H(e^jω)| = 1 for all ω ⇒ ‖y‖² = ‖x‖². For a unit impulse the
    // input energy is 1, so ∑ y[n]² must also converge to 1. Truncate
    // at k·N samples with k large enough that the geometric tail is
    // negligible: remaining energy after k taps is g^{2k}, so k=20 with
    // g=0.7 leaves 0.7^40 ≈ 6.4e-7 → well below any test tolerance.
    ideath::AllpassFilter ap;
    ap.prepare(kSampleRate, 0.05f);
    const int N = 200;
    ap.setDelay(static_cast<float>(N) / kSampleRate);
    const float g = 0.7f;
    ap.setFeedback(g);

    constexpr int kEchoes = 20;
    std::vector<float> y(N * kEchoes + 1);
    y[0] = ap.process(1.0f);
    for (int i = 1; i < static_cast<int>(y.size()); ++i)
        y[i] = ap.process(0.0f);

    double energy = 0.0;
    for (float s : y)
        energy += static_cast<double>(s) * static_cast<double>(s);

    // Tolerance: truncation residual (~6e-7) + accumulated float rounding
    // over 4000 taps (~4000·ε ≈ 5e-4). Round up to 1e-3.
    REQUIRE_THAT(static_cast<float>(energy), WithinAbs(1.0f, 1e-3f));
}

// --- Sinusoidal steady state: RMS is preserved ---

TEST_CASE("AllpassFilter: sinusoid RMS is preserved in steady state", "[allpass]")
{
    // Flat magnitude ⇒ a steady-state sinusoid emerges with the same
    // RMS as it went in (Parseval per-bin). Test at three frequencies
    // that span low / mid / high band.
    for (float freq : { 100.0f, 1000.0f, 8000.0f })
    {
        ideath::AllpassFilter ap;
        ap.prepare(kSampleRate, 0.05f);
        ap.setDelay(0.003f);
        ap.setFeedback(0.6f);

        constexpr int N = 8192;
        std::vector<float> in(N), out(N);
        for (int i = 0; i < N; ++i)
            in[i] = std::sin(2.0f * static_cast<float>(M_PI) * freq
                             * static_cast<float>(i) / kSampleRate);
        for (int i = 0; i < N; ++i)
            out[i] = ap.process(in[i]);

        // Skip the first ~50 ms so the transient response has died out
        // (feedback = 0.6, delay = 132 samples: 20 taps of decay
        // reduces to 0.6^20 ≈ 3.6e-5 of the initial energy — well
        // below the tail). 2205 samples = 50 ms @ 44.1 kHz.
        const int skip = 2205;
        const float rmsIn  = rms(in, skip);
        const float rmsOut = rms(out, skip);

        // 2% tolerance covers residual transient energy leaking into
        // the analysis window plus float rounding over 8k samples.
        REQUIRE_THAT(rmsOut, WithinAbs(rmsIn, rmsIn * 0.02f));
    }
}

// --- Reset ---

TEST_CASE("AllpassFilter: reset clears state", "[allpass]")
{
    ideath::AllpassFilter ap;
    ap.prepare(kSampleRate, 0.05f);
    ap.setDelay(0.005f);
    ap.setFeedback(0.7f);

    // Prime the buffer with an impulse.
    ap.process(1.0f);
    for (int i = 0; i < 1000; ++i)
        ap.process(0.5f);

    ap.reset();

    // After reset the buffer is zeroed. First zero-input sample sees
    // w_delayed = 0, w = 0, y = 0. Exact identity.
    REQUIRE_THAT(ap.process(0.0f), WithinAbs(0.0f, 1e-6f));
}

// --- Parameter clamping ---

TEST_CASE("AllpassFilter: delay clamps to [1, maxDelay]", "[allpass]")
{
    ideath::AllpassFilter ap;
    ap.prepare(kSampleRate, 0.05f); // 2205 samples

    ap.setDelay(1.0f); // 44100 samples requested → clamped to 2205
    REQUIRE(ap.getDelaySamples() <= 2205);
    REQUIRE(ap.getDelaySamples() >= 2200); // rounding tolerance ±5 samples

    ap.setDelay(0.0f); // requests 0 samples → clamped up to 1
    REQUIRE(ap.getDelaySamples() >= 1);
}

TEST_CASE("AllpassFilter: feedback clamps to [-0.99, 0.99]", "[allpass]")
{
    // Test the clamp indirectly via impulse-response first tap:
    // y[0] = -g_clamped, so setting g = 5.0 must yield y[0] = -0.99.
    ideath::AllpassFilter ap;
    ap.prepare(kSampleRate, 0.05f);
    ap.setDelay(0.005f);

    ap.setFeedback(5.0f);
    REQUIRE_THAT(ap.process(1.0f), WithinAbs(-0.99f, 1e-6f));

    ap.reset();
    ap.setFeedback(-5.0f);
    REQUIRE_THAT(ap.process(1.0f), WithinAbs(0.99f, 1e-6f));
}

// --- Long-run stability ---

TEST_CASE("AllpassFilter: long-run stability (10 seconds)", "[allpass]")
{
    // 10 s of continuous processing at high g exercises the denormal
    // guard on the buffer write and confirms the feedback loop stays
    // numerically bounded.
    ideath::AllpassFilter ap;
    ap.prepare(kSampleRate, 0.05f);
    ap.setDelay(0.003f);
    ap.setFeedback(0.9f);

    ap.process(1.0f);
    constexpr int N = 441000;
    for (int i = 1; i < N; ++i)
    {
        float s = ap.process(0.0f);
        REQUIRE(std::isfinite(s));
        // Impulse response is bounded by max|tap| = max(|g|, 1 - g²) ≤ 1
        // for |g| < 1. 1.5 gives generous headroom.
        REQUIRE(std::fabs(s) < 1.5f);
    }
}

// --- Extreme parameter combinations ---

TEST_CASE("AllpassFilter: max feedback + minimum delay stays bounded", "[allpass]")
{
    // g clamped to 0.99, delay clamped up to 1 sample. Dense white
    // noise for 1 s at the pole boundary — the system is still
    // stable (pole inside unit circle) but slowly decaying.
    ideath::AllpassFilter ap;
    ap.prepare(kSampleRate, 0.01f);
    ap.setDelay(0.0f);      // → 1 sample
    ap.setFeedback(2.0f);   // → 0.99

    // xorshift-style deterministic noise so the test is repeatable.
    uint32_t s = 0x1234u;
    auto next = [&]() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(static_cast<int32_t>(s)) / 2.147483648e9f;
    };

    constexpr int N = 44100;
    for (int i = 0; i < N; ++i)
    {
        float y = ap.process(next());
        REQUIRE(std::isfinite(y));
        // Allpass output magnitude in steady state is bounded by
        // input magnitude (RMS equal, peak within a small factor).
        // Noise input in [-1, 1] → output peak within a few units.
        REQUIRE(std::fabs(y) < 5.0f);
    }
}

TEST_CASE("AllpassFilter: negative feedback also produces valid allpass", "[allpass]")
{
    // With g = -0.7: y[0] = -(-0.7)·1 = 0.7; y[N] = -(-0.7)² + 1 = 0.51.
    ideath::AllpassFilter ap;
    ap.prepare(kSampleRate, 0.05f);
    ap.setDelay(0.005f);
    ap.setFeedback(-0.7f);

    const float y0 = ap.process(1.0f);
    REQUIRE_THAT(y0, WithinAbs(0.7f, 1e-6f));
}
