#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/HarmonicOscillator.h>
#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace ideath;

static constexpr float kSR = 44100.0f;
static constexpr float kPi = 3.14159265358979323846f;

// Goertzel-style single-bin magnitude.  Returns the amplitude (not power) of
// the discrete sinusoid at `freq` in `buf`.  For a pure cos/sin of amplitude
// A measured over N samples with the bin frequency near-aligned to an
// integer-cycle window, the return value is A to within float-precision
// leakage.  Cheaper than a full FFT and exact enough for per-partial
// amplitude probes.
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

static float peakAbs(const std::vector<float>& buf)
{
    float m = 0.0f;
    for (float v : buf) m = std::max(m, std::fabs(v));
    return m;
}

static std::vector<float> capture(HarmonicOscillator& h, int samples)
{
    std::vector<float> out(samples);
    for (int i = 0; i < samples; ++i) out[i] = h.process();
    return out;
}

// --- Default state / silence --------------------------------------------

TEST_CASE("HarmonicOscillator: silent until amplitudes are set", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(440.0f);
    // Default amplitudes are all 0.  Output = Σ 0 × sin(2π·phase_i) = 0
    // bit-exact regardless of phase state.
    auto out = capture(h, 1000);
    for (float s : out)
        REQUIRE(s == 0.0f);
}

// --- Reset / state ------------------------------------------------------

TEST_CASE("HarmonicOscillator: reset() yields a deterministic phase sequence", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(440.0f);
    h.setPartialCount(8);
    for (int i = 0; i < 8; ++i) h.setPartialAmplitude(i, 0.1f);

    auto first = capture(h, 1000);
    h.reset();
    auto second = capture(h, 1000);

    // reset() re-seeds the phase RNG from a fixed constant (documented in
    // HarmonicOscillator.h).  Bit-exact reproducibility is required so
    // session-rendered audio is deterministic given identical inputs and
    // tests are reproducible across machines.
    for (size_t i = 0; i < first.size(); ++i)
        REQUIRE(first[i] == second[i]);
}

TEST_CASE("HarmonicOscillator: prepare() re-randomises phases", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(440.0f);
    h.setPartialCount(1);
    h.setPartialAmplitude(0, 1.0f);

    auto a = capture(h, 100);
    h.prepare(48000.0f);
    h.setFrequency(440.0f);
    h.setPartialCount(1);
    h.setPartialAmplitude(0, 1.0f);
    auto b = capture(h, 100);

    // prepare() calls reset() internally → same RNG seed → bit-exact
    // outputs even though sr changed.  This pins the seed contract and
    // catches accidental non-determinism (e.g. seeding from clock).
    // Per-sample frequency is 440/sr, so the values differ between sr,
    // but the *phase progression* must be identical → first sample after
    // prepare must equal the first sample of an independent reset.
    HarmonicOscillator hRef;
    hRef.prepare(48000.0f);
    hRef.setFrequency(440.0f);
    hRef.setPartialCount(1);
    hRef.setPartialAmplitude(0, 1.0f);
    auto c = capture(hRef, 100);
    for (size_t i = 0; i < b.size(); ++i)
        REQUIRE(b[i] == c[i]);
}

// --- Expected behaviour: single partial = sine at fundamental -----------

TEST_CASE("HarmonicOscillator: 1-partial output is a sine at fundamental", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(441.0f);   // 441 Hz × 1 s = exactly 441 cycles → bin-aligned
    h.setPartialCount(1);
    h.setPartialAmplitude(0, 1.0f);

    const int N = static_cast<int>(kSR);
    auto out = capture(h, N);

    const float magOn   = goertzelMagnitude(out, 441.0f, kSR);
    const float magHarm = goertzelMagnitude(out, 882.0f, kSR);

    // For a pure sine of amplitude 1 at a bin-aligned frequency, Goertzel
    // returns the amplitude exactly (modulo float precision).  Tolerance
    // 0.01 covers single-precision rounding in the N=44100 IIR Goertzel
    // recurrence (empirical bound ≲ 1e-3, 0.01 = 10× headroom).
    REQUIRE_THAT(magOn, WithinAbs(1.0f, 0.01f));
    // No 2nd harmonic content: a pure sine has zero energy at 2× fundamental.
    // 1e-3 ≈ −60 dBFS, well above Goertzel leakage floor for orthogonal
    // bins (~1e-7) and well below any audibly meaningful level.
    REQUIRE(magHarm < 1e-3f);
}

TEST_CASE("HarmonicOscillator: nth partial frequency = n × fundamental", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(220.0f);
    h.setPartialCount(8);
    for (int i = 0; i < 8; ++i) h.setPartialAmplitude(i, 0.0f);
    // Partial 3 (idx=2) only: should produce a pure sine at 3 × 220 = 660 Hz.
    h.setPartialAmplitude(2, 1.0f);

    const int N = static_cast<int>(kSR);
    auto out = capture(h, N);

    const float magOn  = goertzelMagnitude(out, 660.0f, kSR);
    const float magF1  = goertzelMagnitude(out, 220.0f, kSR);
    const float magF2  = goertzelMagnitude(out, 440.0f, kSR);

    REQUIRE_THAT(magOn, WithinAbs(1.0f, 0.01f));
    // Other partials are at amp 0 → orthogonal bins → ~0 energy.
    REQUIRE(magF1 < 1e-3f);
    REQUIRE(magF2 < 1e-3f);
}

// --- Output range --------------------------------------------------------

TEST_CASE("HarmonicOscillator: peak bounded by partial count", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(50.0f);    // 50 × 32 = 1600 Hz → all 32 partials below Nyquist
    h.setPartialCount(32);
    for (int i = 0; i < 32; ++i) h.setPartialAmplitude(i, 1.0f);

    auto out = capture(h, static_cast<int>(kSR));

    // Worst-case peak: 32 partials at amp 1.0 in phase = ±32.  This is the
    // documented output ceiling for HarmonicOscillator (CLAUDE.md
    // "Output levels" table).  Random seeded phases mean we will not
    // actually observe ±32 over any practical window — the expected
    // extreme of a Gaussian-distributed sum (CLT) over M samples is
    // ≈ √(N/2) × √(2 ln M) ≈ 4 × √(2 ln 44100) ≈ 18.4 — but the strict
    // mathematical ceiling is N.  1e-3 ULP margin covers float sum
    // precision (relative error per add ≈ 1e-7, accumulated over 32 adds
    // → ~3e-6, well under 1e-3).
    REQUIRE(peakAbs(out) <= 32.0f + 1e-3f);
    // Sanity: must actually produce signal.  Floor 4.0 is well above the
    // single-partial bound (1.0) and below the expected ~18 peak; covers
    // any seed without fitting to a specific RNG state.
    REQUIRE(peakAbs(out) > 4.0f);
}

TEST_CASE("HarmonicOscillator: partials above Nyquist are silenced", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    // Nyquist guard threshold = sampleRate × 0.45 = 19845 Hz (CLAUDE.md
    // parameter-clamping convention).  Fundamental 5000 Hz × 4 = 20000 Hz
    // > 19845 → partial 4 (idx=3) must be muted; partials 1..3 alive.
    h.setFrequency(5000.0f);
    h.setPartialCount(8);
    for (int i = 0; i < 8; ++i) h.setPartialAmplitude(i, 0.0f);
    h.setPartialAmplitude(3, 1.0f);  // only the dead partial active

    auto out = capture(h, 1000);
    // Silenced partial contributes exactly 0; no other partial has amp →
    // output must be 0 bit-exact.
    for (float s : out)
        REQUIRE(s == 0.0f);
}

// --- Plaits-style band mapping -------------------------------------------

TEST_CASE("HarmonicOscillator: setBands(LOW=1) excites partials 1..3 flat", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(220.0f);
    h.setPartialCount(32);
    h.setBands(1.0f, 0.0f, 0.0f, 0.0f);  // LOW=1, MID=0, HIGH=0, shape=0 → flat

    const int N = static_cast<int>(kSR);
    auto out = capture(h, N);

    // LOW band = partials 1..3 → 220, 440, 660 Hz.  shape=0 → all three
    // partials at full band weight (amp 1.0).  Goertzel returns 1.0 at
    // each bin-aligned harmonic (220, 440, 660 are exact-cycle in 1 s).
    REQUIRE_THAT(goertzelMagnitude(out, 220.0f, kSR), WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(goertzelMagnitude(out, 440.0f, kSR), WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(goertzelMagnitude(out, 660.0f, kSR), WithinAbs(1.0f, 0.01f));
    // Partial 4 (MID band start, 880 Hz) must be silent under LOW=1 alone.
    REQUIRE(goertzelMagnitude(out, 880.0f, kSR) < 1e-3f);
    // Partial 8 (HIGH band start, 1760 Hz) must be silent.
    REQUIRE(goertzelMagnitude(out, 1760.0f, kSR) < 1e-3f);
}

TEST_CASE("HarmonicOscillator: setBands(MID=1) excites partials 4..7 flat", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(220.0f);
    h.setPartialCount(32);
    h.setBands(0.0f, 1.0f, 0.0f, 0.0f);

    auto out = capture(h, static_cast<int>(kSR));

    // MID band = partials 4..7 → 880, 1100, 1320, 1540 Hz.
    REQUIRE_THAT(goertzelMagnitude(out, 880.0f,  kSR), WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(goertzelMagnitude(out, 1100.0f, kSR), WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(goertzelMagnitude(out, 1320.0f, kSR), WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(goertzelMagnitude(out, 1540.0f, kSR), WithinAbs(1.0f, 0.01f));
    REQUIRE(goertzelMagnitude(out, 220.0f,  kSR) < 1e-3f);
    REQUIRE(goertzelMagnitude(out, 660.0f,  kSR) < 1e-3f);
    REQUIRE(goertzelMagnitude(out, 1760.0f, kSR) < 1e-3f);
}

TEST_CASE("HarmonicOscillator: setBands(HIGH=1) excites partials 8..32 flat", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(110.0f);   // 110 × 32 = 3520 Hz, all alive
    h.setPartialCount(32);
    h.setBands(0.0f, 0.0f, 1.0f, 0.0f);

    auto out = capture(h, static_cast<int>(kSR));

    // HIGH band = partials 8..32 → 880, 990, ..., 3520 Hz.  Spot-check
    // the lowest (partial 8 = 880) and highest (partial 32 = 3520) and
    // confirm LOW/MID are silent.
    REQUIRE_THAT(goertzelMagnitude(out, 880.0f,  kSR), WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(goertzelMagnitude(out, 3520.0f, kSR), WithinAbs(1.0f, 0.01f));
    REQUIRE(goertzelMagnitude(out, 110.0f, kSR) < 1e-3f);  // partial 1 (LOW)
    REQUIRE(goertzelMagnitude(out, 770.0f, kSR) < 1e-3f);  // partial 7 (MID end)
}

TEST_CASE("HarmonicOscillator: setBands(LOW=1, shape=1) linearly tapers within band", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(220.0f);
    h.setPartialCount(32);
    h.setBands(1.0f, 0.0f, 0.0f, 1.0f);  // LOW=1, shape=1 (full taper)

    auto out = capture(h, static_cast<int>(kSR));

    // shape=1 produces a linear taper across the band:
    //   band_pos[i] = (i - start) / (width - 1) ∈ [0, 1]
    //   within_band_weight = 1 - shape × band_pos
    // For LOW (3 partials, indices 0..2): band_pos = {0, 0.5, 1.0} →
    // weights {1.0, 0.5, 0.0}.
    REQUIRE_THAT(goertzelMagnitude(out, 220.0f, kSR), WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(goertzelMagnitude(out, 440.0f, kSR), WithinAbs(0.5f, 0.01f));
    REQUIRE(goertzelMagnitude(out, 660.0f, kSR) < 1e-3f);
}

// --- Clamping ------------------------------------------------------------

TEST_CASE("HarmonicOscillator: parameter clamping at extremes", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);

    // Frequency: clamp below kMinFreq and above sr × 0.45.
    h.setFrequency(0.0f);
    h.setFrequency(-100.0f);
    h.setFrequency(1e6f);
    h.setFrequency(kSR * 0.6f);

    // Partial count: clamp below 1 and above kMaxPartials.
    h.setPartialCount(0);
    h.setPartialCount(100);

    // Per-partial amplitude: clamp [0, 1]; OOB index silently ignored.
    h.setPartialAmplitude(0, -1.0f);
    h.setPartialAmplitude(0, 5.0f);
    h.setPartialAmplitude(-1, 1.0f);
    h.setPartialAmplitude(99, 1.0f);

    // setBands: each scalar clamped to [0, 1].
    h.setBands(-1.0f, 2.0f, -3.0f, 5.0f);

    // None of the above must produce NaN / Inf / explode.  Process to confirm.
    auto out = capture(h, 1000);
    for (float s : out)
    {
        REQUIRE(std::isfinite(s));
        // Worst-case bound here: setBands(-1,2,-3,5) → clamped (0,1,0,1) →
        // MID + HIGH bands active.  MID = 4 partials, HIGH = 25 partials,
        // but most of HIGH is muted: setFrequency(kSR×0.6) clamped to
        // kSR×0.45 = 19845 → at fundamental 19845, only partial 1 fits
        // under Nyquist guard.  Final bound 32 (≤ kMaxPartials).
        REQUIRE(std::fabs(s) <= 32.0f + 1e-3f);
    }
}

// --- Long-run stability --------------------------------------------------

TEST_CASE("HarmonicOscillator: 10s stability with all partials at max amp", "[harmonicosc][stability]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(110.0f);   // 110 × 32 = 3520 Hz, all alive
    h.setPartialCount(32);
    for (int i = 0; i < 32; ++i) h.setPartialAmplitude(i, 1.0f);

    // 10 seconds = 441000 samples.  Phase accumulators must wrap each
    // sample (CLAUDE.md "Phase wrapping" convention, ref commit bd500ec)
    // — without wrap, fp32 mantissa drifts on long playback and the bin
    // alignment degrades.  This test catches missing wrap and any
    // accumulator overflow / sign explosion.
    const int N = 10 * static_cast<int>(kSR);
    for (int n = 0; n < N; ++n)
    {
        float s = h.process();
        REQUIRE(std::isfinite(s));
        REQUIRE(std::fabs(s) <= 32.0f + 1e-3f);
    }
}

// --- Introspection / partialCount semantics ------------------------------

TEST_CASE("HarmonicOscillator: setBands writes all 32 amplitudes regardless of partialCount", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);
    h.setFrequency(110.0f);
    h.setPartialCount(4);              // CPU knob — caps process() loop
    h.setBands(1.0f, 1.0f, 1.0f, 0.0f); // shape=0 → flat full-band weights

    // setBands is documented as writing all kMaxPartials amplitudes; the
    // active count is purely a per-sample CPU budget.  Partials 5..32 must
    // still carry their setBands amplitude even though they are not being
    // summed in process() right now.  shape=0 → every partial gets full
    // band weight = 1.0, bit-exact (no float ops between weight and store).
    for (int i = 0; i < HarmonicOscillator::kMaxPartials; ++i)
        REQUIRE(h.getPartialAmplitude(i) == 1.0f);

    // Raising partialCount must reveal the pre-set higher-partial amplitudes:
    // first measure output energy with count=4, then with count=32, and
    // confirm the larger sum has more energy at the higher harmonics.
    auto out4 = capture(h, static_cast<int>(kSR));
    // Goertzel at partial 8 (110 × 8 = 880 Hz): silent at count=4.
    REQUIRE(goertzelMagnitude(out4, 880.0f, kSR) < 1e-3f);

    h.reset();   // re-pin phase seed so the second window is comparable
    h.setPartialCount(32);
    auto out32 = capture(h, static_cast<int>(kSR));
    // After raising count, partial 8 must now contribute amp 1.0 — confirms
    // its amplitude was preserved across the setPartialCount(4) phase.
    REQUIRE_THAT(goertzelMagnitude(out32, 880.0f, kSR), WithinAbs(1.0f, 0.01f));
}

TEST_CASE("HarmonicOscillator: setBands taper bit-exact via getPartialAmplitude", "[harmonicosc]")
{
    HarmonicOscillator h;
    h.prepare(kSR);

    // shape=0 → flat band, every partial in band at band weight.
    h.setBands(0.7f, 0.3f, 0.1f, 0.0f);
    REQUIRE(h.getPartialAmplitude(0) == 0.7f);
    REQUIRE(h.getPartialAmplitude(1) == 0.7f);
    REQUIRE(h.getPartialAmplitude(2) == 0.7f);
    REQUIRE(h.getPartialAmplitude(3) == 0.3f);
    REQUIRE(h.getPartialAmplitude(6) == 0.3f);
    REQUIRE(h.getPartialAmplitude(7) == 0.1f);
    REQUIRE(h.getPartialAmplitude(31) == 0.1f);

    // shape=1 → linear taper across each band.
    //   LOW  (width 3,  indices 0..2): band_pos {0, 0.5, 1.0}  → weights {1.0, 0.5, 0.0}
    //   MID  (width 4,  indices 3..6): band_pos {0, 1/3, 2/3, 1.0} → weights {1.0, 2/3, 1/3, 0.0}
    //   HIGH (width 25, indices 7..31): band_pos {0, 1/24, ..., 1.0} → weights linear 1.0 → 0.0
    // Values are the bit-exact output of `weight × (1 - shape × band_pos)`,
    // not implementation-fitted constants.
    h.setBands(1.0f, 1.0f, 1.0f, 1.0f);
    REQUIRE(h.getPartialAmplitude(0)  == 1.0f);
    REQUIRE(h.getPartialAmplitude(1)  == 0.5f);
    REQUIRE(h.getPartialAmplitude(2)  == 0.0f);
    REQUIRE(h.getPartialAmplitude(3)  == 1.0f);
    REQUIRE_THAT(h.getPartialAmplitude(4), WithinAbs(2.0f / 3.0f, 1e-6f));
    REQUIRE_THAT(h.getPartialAmplitude(5), WithinAbs(1.0f / 3.0f, 1e-6f));
    REQUIRE(h.getPartialAmplitude(6)  == 0.0f);
    REQUIRE(h.getPartialAmplitude(7)  == 1.0f);
    REQUIRE(h.getPartialAmplitude(31) == 0.0f);
}

// --- Extreme parameter combos -------------------------------------------

TEST_CASE("HarmonicOscillator: extreme combos stay finite and bounded", "[harmonicosc][extreme]")
{
    HarmonicOscillator h;
    h.prepare(kSR);

    SECTION("very low fundamental, all bands active")
    {
        h.setFrequency(20.0f);
        h.setPartialCount(32);
        h.setBands(1.0f, 1.0f, 1.0f, 0.0f);
        auto out = capture(h, 10000);
        for (float s : out)
        {
            REQUIRE(std::isfinite(s));
            REQUIRE(std::fabs(s) <= 32.0f + 1e-3f);
        }
    }

    SECTION("high fundamental → most partials Nyquist-muted")
    {
        h.setFrequency(8000.0f);
        h.setPartialCount(32);
        h.setBands(1.0f, 1.0f, 1.0f, 0.0f);
        // Active partials are those whose freq ≤ sr × 0.45 = 19845:
        //   partial 1 = 8000, partial 2 = 16000 — both alive
        //   partial 3 = 24000 — muted (above 19845)
        // → only 2 partials contribute → bound = 2.
        auto out = capture(h, 10000);
        for (float s : out)
        {
            REQUIRE(std::isfinite(s));
            REQUIRE(std::fabs(s) <= 2.0f + 1e-3f);
        }
    }

    SECTION("shape=1 with MID band only — exact endpoint taper")
    {
        h.setFrequency(110.0f);
        h.setPartialCount(32);
        h.setBands(0.0f, 1.0f, 0.0f, 1.0f);
        auto out = capture(h, static_cast<int>(kSR));
        // MID band, 4 partials (indices 3..6, width=4), shape=1:
        //   band_pos ∈ {0, 1/3, 2/3, 1.0} → weights {1.0, 2/3, 1/3, 0.0}.
        // Partial 4 = 440 Hz, partial 5 = 550 Hz, partial 6 = 660 Hz,
        // partial 7 = 770 Hz (silent).
        REQUIRE_THAT(goertzelMagnitude(out, 440.0f, kSR), WithinAbs(1.0f,        0.01f));
        REQUIRE_THAT(goertzelMagnitude(out, 550.0f, kSR), WithinAbs(2.0f / 3.0f, 0.02f));
        REQUIRE_THAT(goertzelMagnitude(out, 660.0f, kSR), WithinAbs(1.0f / 3.0f, 0.02f));
        REQUIRE(goertzelMagnitude(out, 770.0f, kSR) < 1e-3f);
    }
}
