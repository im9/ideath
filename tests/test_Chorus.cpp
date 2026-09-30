#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Chorus.h>
#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;
static constexpr float kPi = 3.14159265358979323846f;

namespace {

// Deterministic xorshift32 noise so decorrelation / stability tests
// reproduce bit-exactly across runs.
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

float pearsonCorrelation(const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = a.size();
    double sumA = 0.0, sumB = 0.0;
    for (size_t i = 0; i < n; ++i) { sumA += a[i]; sumB += b[i]; }
    const double meanA = sumA / static_cast<double>(n);
    const double meanB = sumB / static_cast<double>(n);
    double num = 0.0, denA = 0.0, denB = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double da = a[i] - meanA;
        const double db = b[i] - meanB;
        num  += da * db;
        denA += da * da;
        denB += db * db;
    }
    if (denA <= 0.0 || denB <= 0.0) return 0.0f;
    return static_cast<float>(num / std::sqrt(denA * denB));
}

} // namespace

// --- Bypass sanity ---

TEST_CASE("Chorus: mix=0 is exact bypass", "[chorus]")
{
    // With mix = 0 the output is 100% dry regardless of any wet processing.
    // Identity to float rounding.  This is the "wire test" that catches any
    // dry-path bug (missing (1-mix)*in, wrong mix polarity, etc.).
    ideath::Chorus ch;
    ch.prepare(kSampleRate);
    ch.setVoiceCount(3);
    ch.setRate(0.5f);
    ch.setDepth(3.0f);
    ch.setFeedback(0.5f);
    ch.setMix(0.0f);

    // Random-ish deterministic signal so the check exercises every mantissa
    // bit, not just DC.
    Xorshift rng(0xC0FFEEu);
    for (int i = 0; i < 2048; ++i)
    {
        const float in = rng.next();
        const float out = ch.process(in);
        REQUIRE_THAT(out, WithinAbs(in, 1e-6f));
    }
}

// --- Modulation detection (pitch wobble at LFO rate) ---

TEST_CASE("Chorus: depth-modulated delay wobbles at the LFO rate", "[chorus]")
{
    // Setup: one voice, mix = 1 (fully wet), no feedback.  With a ramp
    // input x[n] = n, linear-interpolated read at delay d[n] gives
    // wet[n] = n − d[n] (proven algebraically for a ramp — the interpolation
    // fraction reproduces the exact fractional offset).  So the sample-wise
    // delay d[n] = n − wet[n] can be extracted directly and its periodicity
    // measured.
    //
    // The extracted d[n] is a sinusoid at the LFO rate (constant base delay
    // + depth·sin(2π f_LFO t)).  Zero-crossing counting of (d[n] − mean)
    // is a robust, threshold-tolerant way to verify the modulation cycles
    // at f_LFO ±10%.
    ideath::Chorus ch;
    ch.prepare(kSampleRate);
    ch.setVoiceCount(1);
    ch.setRate(0.5f);       // 0.5 Hz LFO
    ch.setDepth(5.0f);      // 5 ms depth (well below base ~12 ms so d[n] > 0)
    ch.setSpread(0.0f);
    ch.setFeedback(0.0f);
    ch.setMix(1.0f);

    // Warm up long enough that the read tap always lands inside written
    // data: base + depth = 17 ms → 750 samples.  1000 sample warmup safely
    // covers it.
    for (int i = 0; i < 1000; ++i)
        ch.process(static_cast<float>(i));

    // Analyse the next 30 s = 15 LFO cycles.
    const int   analyseFrom = 1000;
    const int   analyseLen  = static_cast<int>(30.0f * kSampleRate);
    std::vector<float> d(static_cast<size_t>(analyseLen));
    for (int i = 0; i < analyseLen; ++i)
    {
        const int   n   = analyseFrom + i;
        const float out = ch.process(static_cast<float>(n));
        d[static_cast<size_t>(i)] = static_cast<float>(n) - out;
    }

    // Compute mean of d[n] then count zero crossings of (d[n] − mean).
    // Each LFO cycle contributes exactly 2 zero crossings (rising + falling
    // through mean).  15 cycles → 30 crossings expected.
    double sum = 0.0;
    for (float v : d) sum += v;
    const float mean = static_cast<float>(sum / static_cast<double>(d.size()));

    int crossings = 0;
    float prev = d[0] - mean;
    for (size_t i = 1; i < d.size(); ++i)
    {
        const float cur = d[i] - mean;
        if ((prev <= 0.0f && cur > 0.0f) || (prev >= 0.0f && cur < 0.0f))
            ++crossings;
        prev = cur;
    }

    // 15 cycles × 2 crossings/cycle = 30.  ±10 % → [27, 33].
    // Derivation: rate spec says "within ±10 %", 10 % of 30 = 3.
    REQUIRE(crossings >= 27);
    REQUIRE(crossings <= 33);

    // Sanity: the modulation amplitude should be ≈ depth samples.
    // 5 ms × 44100 = 220.5 samples.  Peak-to-peak of d[n] should be ≈ 441.
    // Allow wide tolerance (±20 %) because the LFO waveform lookup shape and
    // buffer wraparound edges introduce small deviations.
    float dMin = d[0], dMax = d[0];
    for (float v : d) { dMin = std::min(dMin, v); dMax = std::max(dMax, v); }
    const float pkpk = dMax - dMin;
    REQUIRE(pkpk > 350.0f); // 441 × 0.8 = 353
    REQUIRE(pkpk < 530.0f); // 441 × 1.2 = 529
}

// --- Feedback stability ---

TEST_CASE("Chorus: bounded output under heavy feedback", "[chorus]")
{
    // 60 s of continuous processing with feedback = 0.5, mix = 1.
    // We prime the loop with 1 s of noise so the feedback bus is non-empty,
    // then let it decay under silence for 60 s more.  Peak must stay below
    // the documented ±1.3 ceiling and no NaN/Inf may appear (denormal guard
    // + std::isfinite check).
    ideath::Chorus ch;
    ch.prepare(kSampleRate);
    ch.setVoiceCount(3);
    ch.setRate(0.5f);
    ch.setDepth(5.0f);
    ch.setFeedback(0.5f);
    ch.setMix(1.0f);

    Xorshift rng(0xBEEFu);
    const int primeN = static_cast<int>(kSampleRate);
    for (int i = 0; i < primeN; ++i)
        ch.process(rng.next());

    float peak = 0.0f;
    const int sustainN = static_cast<int>(60.0f * kSampleRate);
    for (int i = 0; i < sustainN; ++i)
    {
        const float s = ch.process(0.0f);
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::fabs(s));
    }

    // Ceiling justification: documented ±1.3 peak.  Silent-input decay
    // should stay well inside that.  Use the full ceiling as the assertion.
    REQUIRE(peak < 1.3f);
}

// --- Stereo decorrelation via spread ---

TEST_CASE("Chorus: spread=1 decorrelates L/R", "[chorus]")
{
    // Voices are panned across L/R by index; when their LFO phases align
    // (spread = 0) the two channels sum the *same* time-modulated signal
    // and Pearson correlation is near 1.  When phases are spread across
    // the LFO cycle (spread = 1) the sums differ and correlation drops.
    //
    // Threshold: 0.5.  Justification: 2 voices at maximally opposed LFO
    // phase (spread = 1, N = 2) give L = voice0, R = voice1, whose
    // instantaneous delays are 180° out of phase in the LFO cycle → the
    // time-shifted noise signals are essentially decorrelated (< 0.3
    // typical).  Even with 3-4 voices distributed unevenly the correlation
    // stays well under 0.5.
    ideath::Chorus ch;
    ch.prepare(kSampleRate);
    ch.setVoiceCount(4);
    ch.setRate(3.0f);        // faster LFO so 1 s of analysis covers ≥ 3 cycles
    ch.setDepth(8.0f);
    ch.setSpread(1.0f);
    ch.setFeedback(0.0f);
    ch.setMix(1.0f);

    // Warm up 100 ms so buffer & LFO reach steady state.
    Xorshift rng(0xF00Du);
    for (int i = 0; i < static_cast<int>(0.1f * kSampleRate); ++i)
    {
        float l, r;
        ch.processStereo(rng.next(), l, r);
    }

    const int N = static_cast<int>(kSampleRate); // 1 s
    std::vector<float> L(static_cast<size_t>(N)), R(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        ch.processStereo(rng.next(), L[static_cast<size_t>(i)], R[static_cast<size_t>(i)]);

    const float corr = pearsonCorrelation(L, R);
    REQUIRE(corr < 0.5f);

    // Sanity: spread = 0 gives near-identical channels (correlation ≈ 1).
    // Reset primitive, drive with same seed, verify.
    ideath::Chorus ch2;
    ch2.prepare(kSampleRate);
    ch2.setVoiceCount(4);
    ch2.setRate(3.0f);
    ch2.setDepth(8.0f);
    ch2.setSpread(0.0f);     // no voice-to-voice offset
    ch2.setFeedback(0.0f);
    ch2.setMix(1.0f);

    Xorshift rng2(0xF00Du);
    for (int i = 0; i < static_cast<int>(0.1f * kSampleRate); ++i)
    {
        float l, r;
        ch2.processStereo(rng2.next(), l, r);
    }
    std::vector<float> L0(static_cast<size_t>(N)), R0(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        ch2.processStereo(rng2.next(), L0[static_cast<size_t>(i)], R0[static_cast<size_t>(i)]);

    const float corr0 = pearsonCorrelation(L0, R0);
    // With spread=0 every voice modulates in lockstep — L and R receive
    // the same wet bus (just via different pan gains) so correlation is
    // essentially 1.  0.95 leaves float rounding + pan-law noise room.
    REQUIRE(corr0 > 0.95f);
}

// --- Parameter clamping ---

TEST_CASE("Chorus: setVoiceCount clamps to [1, kMaxVoices]", "[chorus]")
{
    ideath::Chorus ch;
    ch.prepare(kSampleRate);

    ch.setVoiceCount(0);
    REQUIRE(ch.getVoiceCount() >= 1);

    ch.setVoiceCount(-5);
    REQUIRE(ch.getVoiceCount() >= 1);

    ch.setVoiceCount(1000);
    REQUIRE(ch.getVoiceCount() <= ideath::Chorus::kMaxVoices);
}

TEST_CASE("Chorus: setters accept extreme values without producing NaN", "[chorus]")
{
    // Setters must clamp before storing.  A follow-up process() must not
    // produce non-finite output regardless of what the caller passed.
    ideath::Chorus ch;
    ch.prepare(kSampleRate);

    ch.setRate(-100.0f);
    ch.setRate(1000.0f);
    ch.setDepth(-5.0f);
    ch.setDepth(1000.0f);
    ch.setSpread(-1.0f);
    ch.setSpread(5.0f);
    ch.setFeedback(-10.0f);
    ch.setFeedback(10.0f);
    ch.setMix(-1.0f);
    ch.setMix(5.0f);

    for (int i = 0; i < 4096; ++i)
    {
        const float in = static_cast<float>((i % 100) - 50) * 0.02f;
        const float out = ch.process(in);
        REQUIRE(std::isfinite(out));
    }
}

// --- Reset ---

TEST_CASE("Chorus: reset clears buffer and phase", "[chorus]")
{
    ideath::Chorus ch;
    ch.prepare(kSampleRate);
    ch.setVoiceCount(3);
    ch.setRate(2.0f);
    ch.setDepth(5.0f);
    ch.setFeedback(0.5f);
    ch.setMix(1.0f);

    // Prime with signal.
    for (int i = 0; i < 2000; ++i)
        ch.process(0.5f);

    ch.reset();

    // Post-reset: silence in for the initial "delay-line lookback" window
    // must give silence out (no ghost tail from the pre-reset session).
    // Base delay ~12 ms + depth 5 ms = 17 ms → 750 samples.  Read 400 samples
    // of output while feeding silence and require it stays at 0.  We only
    // check the initial window (before the modulated read tap would circle
    // back to whatever pre-reset state might remain).
    for (int i = 0; i < 400; ++i)
    {
        const float out = ch.process(0.0f);
        REQUIRE_THAT(out, WithinAbs(0.0f, 1e-6f));
    }
}
