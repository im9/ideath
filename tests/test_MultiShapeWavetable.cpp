#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/MultiShapeWavetable.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using ideath::MultiShapeWavetable;

static constexpr float kSampleRate = 44100.0f;
static constexpr float kPi = 3.14159265358979323846f;

// Goertzel-style single-bin amplitude estimator.
// Returns the peak amplitude of a sinusoid at `targetHz` present in `buf[0..n)`.
// Result: |X(targetHz)| * 2 / N (i.e. peak amplitude of that frequency component),
// so a unit sine at exactly `targetHz` yields ~1.0.
static double binAmplitude(const float* buf, int n, double sr, double targetHz)
{
    const double w = 2.0 * 3.14159265358979323846 * targetHz / sr;
    const double cw = std::cos(w);
    const double sw = std::sin(w);
    double re = 0.0;
    double im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i);
        re += static_cast<double>(buf[i]) * std::cos(w * t);
        im -= static_cast<double>(buf[i]) * std::sin(w * t);
    }
    (void)cw; (void)sw;
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

// ---------------------------------------------------------------------------
// Construction / lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("MultiShapeWavetable: default constructible, prepare initialises", "[multiwt]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);

    // process() must produce finite output even before any setShape call
    // (default position 0 = Sine).
    for (int i = 0; i < 1000; ++i)
    {
        float s = wt.process();
        REQUIRE(std::isfinite(s));
    }
}

TEST_CASE("MultiShapeWavetable: reset zeroes phase", "[multiwt]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);

    for (int i = 0; i < 500; ++i) wt.process();
    REQUIRE(wt.getPhase() > 0.0f);

    wt.reset();
    REQUIRE_THAT(wt.getPhase(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("MultiShapeWavetable: reset preserves frequency and shape", "[multiwt]")
{
    // Library convention: reset = zero state, preserve configuration.
    // Matches Biquad / Wavetable / SVFilter behaviour.
    MultiShapeWavetable a, b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    a.setFrequency(440.0f);
    b.setFrequency(440.0f);
    a.setShape(MultiShapeWavetable::Shape::Saw);
    b.setShape(MultiShapeWavetable::Shape::Saw);

    for (int i = 0; i < 500; ++i) a.process();
    a.reset();

    for (int i = 0; i < 16; ++i)
    {
        float sa = a.process();
        float sb = b.process();
        REQUIRE_THAT(sa, WithinAbs(sb, 1e-6f));
    }
}

// ---------------------------------------------------------------------------
// Output range — every shape must stay within ±1.05.
// Tables are peak-normalised to 1.0 inside generateTable(); linear interp
// between adjacent samples cannot exceed the maximum table sample (1.0).
// The 0.05 headroom accounts for shape morph crossfade which can briefly
// sum two shapes whose peaks land near each other in phase.
// ---------------------------------------------------------------------------

TEST_CASE("MultiShapeWavetable: output stays in [-1.05, 1.05] for every shape", "[multiwt]")
{
    const int n = static_cast<int>(MultiShapeWavetable::shapeCount());
    for (int s = 0; s < n; ++s)
    {
        MultiShapeWavetable wt;
        wt.prepare(kSampleRate);
        wt.setFrequency(440.0f);
        wt.setShape(static_cast<MultiShapeWavetable::Shape>(s));

        constexpr int N = 44100;
        float peak = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            float v = std::abs(wt.process());
            if (v > peak) peak = v;
        }
        INFO("shape index = " << s);
        REQUIRE(peak <= 1.05f);
        // Anti-silence: every shape must be audible at +440 Hz, position-matched.
        // Floor 0.1 = -20 dB; well below any real shape's amplitude after peak
        // normalisation (worst case Spectral with 1/h² rolloff still has h=1
        // dominating the time-domain peak at ≈ 0.85 of unity).
        REQUIRE(peak >= 0.1f);
    }
}

// ---------------------------------------------------------------------------
// Frequency correctness
// ---------------------------------------------------------------------------

TEST_CASE("MultiShapeWavetable: sine frequency via zero crossings", "[multiwt]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);
    wt.setShape(MultiShapeWavetable::Shape::Sine);

    constexpr int N = 44100;
    int crossings = 0;
    float prev = wt.process();
    for (int i = 1; i < N; ++i)
    {
        float s = wt.process();
        if ((prev >= 0.0f && s < 0.0f) || (prev < 0.0f && s >= 0.0f))
            ++crossings;
        prev = s;
    }

    // 440 Hz × 2 crossings/cycle = 880 crossings/second. 44100/440 = 100.227
    // → 440 full cycles plus 0.227 of one more, so 880 ±2 covers the partial
    // cycle at the window edge.
    REQUIRE(crossings >= 878);
    REQUIRE(crossings <= 882);
}

// ---------------------------------------------------------------------------
// Spectral content per shape (DFT bin amplitudes)
// ---------------------------------------------------------------------------

TEST_CASE("MultiShapeWavetable: Sine has only fundamental", "[multiwt][spectrum]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    const float f0 = 100.0f; // integer cycles per second window → no leakage
    wt.setFrequency(f0);
    wt.setShape(MultiShapeWavetable::Shape::Sine);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = wt.process();

    const double a1 = binAmplitude(buf.data(), N, kSampleRate, f0);
    const double a2 = binAmplitude(buf.data(), N, kSampleRate, 2.0 * f0);
    const double a3 = binAmplitude(buf.data(), N, kSampleRate, 3.0 * f0);

    // Pure sine: fundamental ≈ 1.0 (peak amp), all other harmonics ≈ 0.
    // Tolerance 0.05 for fundamental: table is 2048-sample additive sine,
    // peak normalisation = unity exactly, but DFT bin width vs phase
    // discretisation costs ≲ 1%.  Other harmonics floor at < 1% (-40 dB);
    // this also bounds any residual DC + numerical noise.
    REQUIRE(a1 > 0.95);
    REQUIRE(a1 < 1.05);
    REQUIRE(a2 < 0.01);
    REQUIRE(a3 < 0.01);
}

TEST_CASE("MultiShapeWavetable: Saw has all harmonics with 1/h rolloff", "[multiwt][spectrum]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    const float f0 = 100.0f;
    wt.setFrequency(f0);
    wt.setShape(MultiShapeWavetable::Shape::Saw);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = wt.process();

    const double a1 = binAmplitude(buf.data(), N, kSampleRate, f0);
    const double a2 = binAmplitude(buf.data(), N, kSampleRate, 2.0 * f0);
    const double a3 = binAmplitude(buf.data(), N, kSampleRate, 3.0 * f0);
    const double a4 = binAmplitude(buf.data(), N, kSampleRate, 4.0 * f0);

    // Additive saw: harmonic amplitudes ∝ 1/h.  Peak normalisation scales all
    // bins uniformly so the RATIO a2/a1 ≈ 1/2, a4/a2 ≈ 1/2 is preserved
    // regardless of the absolute peak.  Tolerance ±15%: peak normalisation
    // depends on the time-domain peak which is dominated by the fundamental
    // but receives small contributions from all harmonics, so the per-bin
    // scaling deviates a few percent from 1/h analytic.
    REQUIRE(a1 > 0.4);                    // fundamental non-trivial
    REQUIRE(a2 / a1 > 0.40); REQUIRE(a2 / a1 < 0.60);   // ≈ 0.5
    REQUIRE(a3 / a1 > 0.27); REQUIRE(a3 / a1 < 0.40);   // ≈ 0.333
    REQUIRE(a4 / a1 > 0.20); REQUIRE(a4 / a1 < 0.30);   // ≈ 0.25
}

TEST_CASE("MultiShapeWavetable: Square has only odd harmonics", "[multiwt][spectrum]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    const float f0 = 100.0f;
    wt.setFrequency(f0);
    wt.setShape(MultiShapeWavetable::Shape::Square);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = wt.process();

    const double a1 = binAmplitude(buf.data(), N, kSampleRate, f0);
    const double a2 = binAmplitude(buf.data(), N, kSampleRate, 2.0 * f0);
    const double a3 = binAmplitude(buf.data(), N, kSampleRate, 3.0 * f0);
    const double a4 = binAmplitude(buf.data(), N, kSampleRate, 4.0 * f0);
    const double a5 = binAmplitude(buf.data(), N, kSampleRate, 5.0 * f0);

    // Square = additive odd harmonics only with amp ∝ 1/h.
    // Even harmonics are explicitly 0 in the additive sum, so a2/a1 and a4/a1
    // should be at numerical-noise floor.  Threshold 0.02 = -34 dB is loose
    // enough for any windowing leakage from adjacent odd bins (sinc sidelobes
    // of bin 100 evaluated at bin 200 are < 0.01).
    REQUIRE(a1 > 0.5);
    REQUIRE(a2 < 0.02);
    REQUIRE(a4 < 0.02);
    // Odd harmonics rolloff ≈ 1/h:  a3/a1 ≈ 1/3, a5/a1 ≈ 1/5
    REQUIRE(a3 / a1 > 0.27); REQUIRE(a3 / a1 < 0.40);
    REQUIRE(a5 / a1 > 0.16); REQUIRE(a5 / a1 < 0.25);
}

TEST_CASE("MultiShapeWavetable: Triangle has only odd harmonics with 1/h² rolloff", "[multiwt][spectrum]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    const float f0 = 100.0f;
    wt.setFrequency(f0);
    wt.setShape(MultiShapeWavetable::Shape::Triangle);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = wt.process();

    const double a1 = binAmplitude(buf.data(), N, kSampleRate, f0);
    const double a2 = binAmplitude(buf.data(), N, kSampleRate, 2.0 * f0);
    const double a3 = binAmplitude(buf.data(), N, kSampleRate, 3.0 * f0);
    const double a5 = binAmplitude(buf.data(), N, kSampleRate, 5.0 * f0);

    // Triangle = additive odd harmonics with amp ∝ 1/h².
    // a3/a1 = 1/9 ≈ 0.111, a5/a1 = 1/25 = 0.04.
    REQUIRE(a1 > 0.5);
    REQUIRE(a2 < 0.02);
    REQUIRE(a3 / a1 > 0.08); REQUIRE(a3 / a1 < 0.14);
    REQUIRE(a5 / a1 > 0.025); REQUIRE(a5 / a1 < 0.06);
}

TEST_CASE("MultiShapeWavetable: Spectral has odd harmonics with 1/h² rolloff", "[multiwt][spectrum]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    const float f0 = 100.0f;
    wt.setFrequency(f0);
    wt.setShape(MultiShapeWavetable::Shape::Spectral);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = wt.process();

    const double a1 = binAmplitude(buf.data(), N, kSampleRate, f0);
    const double a2 = binAmplitude(buf.data(), N, kSampleRate, 2.0 * f0);
    const double a3 = binAmplitude(buf.data(), N, kSampleRate, 3.0 * f0);

    // Spectral uses amp = (h odd) ? 1/(h² × 0.5) : 0 — same 1/h² envelope as
    // Triangle but using the inboil contract (steeper than 1/h² because of the
    // 0.5 factor making h=1 dominant after peak normalisation).
    // a3/a1 ≈ 1/9.  No even harmonics.
    REQUIRE(a1 > 0.5);
    REQUIRE(a2 < 0.02);
    REQUIRE(a3 / a1 > 0.08); REQUIRE(a3 / a1 < 0.14);
}

TEST_CASE("MultiShapeWavetable: Pulse has near-flat harmonic spectrum", "[multiwt][spectrum]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    const float f0 = 100.0f;
    wt.setFrequency(f0);
    wt.setShape(MultiShapeWavetable::Shape::Pulse);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = wt.process();

    const double a1 = binAmplitude(buf.data(), N, kSampleRate, f0);
    const double a2 = binAmplitude(buf.data(), N, kSampleRate, 2.0 * f0);
    const double a3 = binAmplitude(buf.data(), N, kSampleRate, 3.0 * f0);
    const double a5 = binAmplitude(buf.data(), N, kSampleRate, 5.0 * f0);

    // Pulse is implemented as "all harmonics, constant amplitude" — the Fourier
    // description of a band-limited narrow pulse (Dirac comb).  After peak
    // normalisation the time-domain peak (which scales with harmonic count) is
    // pulled down so each harmonic gets the same small amplitude.  Signature:
    // a2/a1 ≈ 1.0 and a3/a1 ≈ 1.0 — distinct from every other shape (Saw=0.5,
    // Square=0, Triangle=0, Spectral=0).
    REQUIRE(a1 > 0.01);
    REQUIRE(a2 / a1 > 0.7);  REQUIRE(a2 / a1 < 1.3);
    REQUIRE(a3 / a1 > 0.7);  REQUIRE(a3 / a1 < 1.3);
    REQUIRE(a5 / a1 > 0.7);  REQUIRE(a5 / a1 < 1.3);
}

TEST_CASE("MultiShapeWavetable: Metallic has irregular harmonic distribution", "[multiwt][spectrum]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    const float f0 = 100.0f;
    wt.setFrequency(f0);
    wt.setShape(MultiShapeWavetable::Shape::Metallic);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = wt.process();

    // Metallic writes non-integer-cycle sines (ratios 1, 1.47, 2.09, 2.56, 3.14, 4.2
    // from inboil L530) into a 2048-sample loop.  When looped at f0, the table's
    // periodicity forces the spectrum onto integer-bin harmonics of f0 — the
    // signature is therefore NOT "peak at 1.47×f0" but an irregular distribution
    // across integer bins, where some higher harmonic rivals the fundamental
    // (because the 1.47×, 2.09×, 2.56× partials all leak energy into bins 2-3-4
    // via the table-loop discontinuity).
    const double a1 = binAmplitude(buf.data(), N, kSampleRate, 1.0 * f0);
    const double a2 = binAmplitude(buf.data(), N, kSampleRate, 2.0 * f0);
    const double a3 = binAmplitude(buf.data(), N, kSampleRate, 3.0 * f0);
    const double a4 = binAmplitude(buf.data(), N, kSampleRate, 4.0 * f0);
    const double a5 = binAmplitude(buf.data(), N, kSampleRate, 5.0 * f0);

    // Saw / Square / Triangle / Spectral / Sine all have STRICTLY DECREASING
    // harmonic amplitudes (the 1/h or 1/h² envelope is monotone).  Metallic's
    // partials sit at non-integer cycle counts in the table — each leaks energy
    // into multiple integer bins via the table-loop discontinuity, producing
    // an irregular spectrum where some higher harmonic exceeds its predecessor.
    //
    // Empirically (sr=44100, f0=100) the strongest non-monotonicity is at
    // h=5 > h=4 (the 4.2 partial's leak into bin 4 partially cancels with
    // other partials' contributions there, while bin 5 receives net positive
    // sum from the 4.2 + 5.x neighborhood).  This single inequality is enough
    // to distinguish Metallic from every other 1/h or 1/h² shape.
    REQUIRE(a1 > 0.1);
    REQUIRE(a5 > a4);
    // And the spectrum still has substantial mid-harmonic content (not silent
    // beyond h=2 — that would be sine).  Sum a2..a5 > 30% of a1 confirms
    // the harmonic richness that gives Metallic its bell-like character.
    REQUIRE((a2 + a3 + a4 + a5) > a1 * 0.5);
}

TEST_CASE("MultiShapeWavetable: FormantA and FormantO have distinct spectra with multiple peaks", "[multiwt][spectrum]")
{
    // Important: formant peaks are placed at ABSOLUTE Hz (730/1090/2440 for A,
    // 570/840/2410 for O) during table generation against the mipmap's baseFreq,
    // so the playback spectrum's formant locations PITCH-TRACK with the played
    // fundamental (same limitation as inboil — a wavetable formant is not a
    // fixed-frequency resonator).  Instead of asserting peak position, we verify
    // the two formant shapes produce measurably different spectra AND each has
    // non-monotonic harmonic distribution (= multiple peaks, the hallmark of a
    // formant shape vs a simple 1/h saw).
    MultiShapeWavetable a, o;
    a.prepare(kSampleRate);
    o.prepare(kSampleRate);
    // f0=64 lands on mipmap level 1 boundary where baseFreq=64, so the table-h
    // and absolute Hz line up cleanly for inspection.
    const float f0 = 64.0f;
    a.setFrequency(f0);
    o.setFrequency(f0);
    a.setShape(MultiShapeWavetable::Shape::FormantA);
    o.setShape(MultiShapeWavetable::Shape::FormantO);

    constexpr int N = 44100;
    std::vector<float> bufA(N), bufO(N);
    for (int i = 0; i < N; ++i) { bufA[i] = a.process(); bufO[i] = o.process(); }

    // Distinctness — sum of per-bin absolute differences over the first 40 harmonics
    // (covers F1 region for both shapes).  If FormantA == FormantO this is ~0.
    double diff = 0.0;
    for (int h = 1; h <= 40; ++h)
    {
        const double xa = binAmplitude(bufA.data(), N, kSampleRate, h * f0);
        const double xo = binAmplitude(bufO.data(), N, kSampleRate, h * f0);
        diff += std::abs(xa - xo);
    }
    // Threshold 0.1 ≈ -20 dB cumulative: any meaningful formant differentiation
    // produces several harmonics with >0.05 amplitude difference each.
    REQUIRE(diff > 0.1);

    // Multi-peak check: at least one mid-range harmonic (h=10..40) of FormantA
    // exceeds 30% of the loudest harmonic in h=1..40.  A plain 1/h saw shape's
    // h=10 sits at 1/10 = 0.1 of the fundamental, well below this threshold.
    double peakA = 0.0;
    double peakMidA = 0.0;
    for (int h = 1; h <= 40; ++h)
    {
        const double x = binAmplitude(bufA.data(), N, kSampleRate, h * f0);
        if (x > peakA) peakA = x;
        if (h >= 10 && x > peakMidA) peakMidA = x;
    }
    REQUIRE(peakMidA > peakA * 0.3);
}

TEST_CASE("MultiShapeWavetable: SuperSaw has high-harmonic notching vs plain Saw", "[multiwt][spectrum]")
{
    MultiShapeWavetable saw;
    MultiShapeWavetable super;
    saw.prepare(kSampleRate);
    super.prepare(kSampleRate);
    const float f0 = 100.0f;
    saw.setFrequency(f0);
    super.setFrequency(f0);
    saw.setShape(MultiShapeWavetable::Shape::Saw);
    super.setShape(MultiShapeWavetable::Shape::SuperSaw);

    constexpr int N = 44100;
    std::vector<float> sawBuf(N), superBuf(N);
    for (int i = 0; i < N; ++i)
    {
        sawBuf[i]   = saw.process();
        superBuf[i] = super.process();
    }

    // SuperSaw is implemented as 7 phase-offset saws summed inside ONE wavetable
    // (inboil melodic.ts L471-484, phaseOff = detune * h * 2.5).  Because the
    // table loops periodically at f0, the harmonics still land on integer bins —
    // SuperSaw does NOT produce inter-bin energy.  The audible signature is
    // amplitude notching at high harmonics: per harmonic h, the 7 detune-offset
    // contributions sum coherently at low h (sum cos(small angles) ≈ 7) but
    // cancel as h grows.  At h=10, expected per-harmonic gain ≈ |sum cos(h·dt·2.5)|/7
    // ≈ |cos(3)·2 + cos(2)·2 + cos(1)·2 + 1| / 7 ≈ 0.10.
    //
    // So a10/a1 for SuperSaw should be much smaller than Saw's 1/10 = 0.1.
    // Specifically: (SuperSaw a10/a1) < 0.5 × (Saw a10/a1) is the signature.
    const double sawA1 = binAmplitude(sawBuf.data(), N, kSampleRate, 1.0 * f0);
    const double sawA10 = binAmplitude(sawBuf.data(), N, kSampleRate, 10.0 * f0);
    const double superA1 = binAmplitude(superBuf.data(), N, kSampleRate, 1.0 * f0);
    const double superA10 = binAmplitude(superBuf.data(), N, kSampleRate, 10.0 * f0);

    const double sawRatio   = sawA10   / sawA1;     // ≈ 0.1
    const double superRatio = superA10 / superA1;   // expected ≪ 0.1

    REQUIRE(sawRatio > 0.07);                       // sanity-check Saw is normal
    REQUIRE(superRatio < sawRatio * 0.5);           // SuperSaw notches h=10
}

// ---------------------------------------------------------------------------
// Anti-aliasing — the core promise of mipmap band-limiting
// ---------------------------------------------------------------------------

TEST_CASE("MultiShapeWavetable: high-freq Saw has no aliased content above Nyquist", "[multiwt][antialias]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    // 4000 Hz fundamental.  Under the alias-safe selector this picks level 7
    // (baseFreq=4096, maxHarmonics ≈ 5 — exactly the harmonics 1..5 that
    // playback at 4000 Hz can hold within Nyquist=22050).  A naive saw at
    // 4000 Hz aliases catastrophically (harmonic 6 = 24000 folds to 20100,
    // harmonic 10 = 40000 folds to 4100, etc.) — those folded bins are what
    // we explicitly check here.
    wt.setFrequency(4000.0f);
    wt.setShape(MultiShapeWavetable::Shape::Saw);

    constexpr int N = 44100;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = wt.process();

    const double a4000 = binAmplitude(buf.data(), N, kSampleRate, 4000.0);
    REQUIRE(a4000 > 0.2);

    // The predicted alias frequencies for naive saw harmonics h=6..10 of
    // 4000 Hz fundamental at sr=44100:
    //   h=6:  24000 → sr - 24000 = 20100 Hz
    //   h=7:  28000 → sr - 28000 = 16100 Hz
    //   h=8:  32000 → sr - 32000 = 12100 Hz
    //   h=9:  36000 → sr - 36000 =  8100 Hz
    //   h=10: 40000 → sr - 40000 =  4100 Hz
    // Each would arrive at amplitude 1/h ≈ 0.1 under the naive saw — that's
    // -20 dB, deeply audible.  Threshold -40 dB (0.01 of fundamental) is the
    // pass criterion: anything above that means the mipmap selection failed.
    const double bins[] = { 4100.0, 8100.0, 12100.0, 16100.0, 20100.0 };
    for (double f : bins)
    {
        const double a = binAmplitude(buf.data(), N, kSampleRate, f);
        INFO("aliased bin " << f);
        REQUIRE(a < a4000 * 0.01);
    }
}

// ---------------------------------------------------------------------------
// Mipmap level selection
// ---------------------------------------------------------------------------

TEST_CASE("MultiShapeWavetable: mipmapLevelFor picks alias-safe level", "[multiwt]")
{
    // Selection rule: smallest k where baseFreq[k] >= freq.  This is the level
    // whose table's harmonic count guarantees max_h × freq <= nyquist.
    // Boundaries (baseFreq = [32, 64, 128, 256, 512, 1024, 2048, 4096, 8192]):
    //   freq ≤ 32       → 0
    //   32 < freq ≤ 64  → 1
    //   64 < freq ≤ 128 → 2  (etc.)
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(20.0f)    == 0);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(32.0f)    == 0);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(33.0f)    == 1);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(64.0f)    == 1);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(65.0f)    == 2);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(128.0f)   == 2);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(129.0f)   == 3);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(256.0f)   == 3);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(1024.0f)  == 5);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(1025.0f)  == 6);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(8192.0f)  == 8);
    REQUIRE(MultiShapeWavetable::mipmapLevelFor(20000.0f) == 8); // above 8192: cap
}

// ---------------------------------------------------------------------------
// Shape morphing
// ---------------------------------------------------------------------------

TEST_CASE("MultiShapeWavetable: shape position 0.5 mixes adjacent shapes", "[multiwt]")
{
    // Position 0 = Sine, position 1 = Square.  Position 0.5 should produce a
    // 50/50 mix: smaller than full Square but bigger fundamental than full Sine
    // (because the square contribution adds amplitude at higher harmonics too).
    MultiShapeWavetable a, b, mix;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    mix.prepare(kSampleRate);
    const float f0 = 100.0f;
    a.setFrequency(f0);
    b.setFrequency(f0);
    mix.setFrequency(f0);
    a.setShape(MultiShapeWavetable::Shape::Sine);
    b.setShape(MultiShapeWavetable::Shape::Square);
    mix.setShapePosition(0.5f);

    constexpr int N = 44100;
    std::vector<float> bufA(N), bufB(N), bufMix(N);
    for (int i = 0; i < N; ++i) { bufA[i] = a.process(); bufB[i] = b.process(); bufMix[i] = mix.process(); }

    const double h3_A   = binAmplitude(bufA.data(),   N, kSampleRate, 300.0);
    const double h3_B   = binAmplitude(bufB.data(),   N, kSampleRate, 300.0);
    const double h3_Mix = binAmplitude(bufMix.data(), N, kSampleRate, 300.0);

    // Sine has no 3rd harmonic, Square has a strong 3rd.  50/50 morph should
    // have ≈ half the 3rd-harmonic energy of pure Square.  ±25% tolerance
    // covers small peak-normalisation shifts inside each per-shape table
    // (each table is normalised independently before mixing).
    REQUIRE(h3_A < 0.02);
    REQUIRE(h3_B > 0.2);
    REQUIRE(h3_Mix > h3_B * 0.35);
    REQUIRE(h3_Mix < h3_B * 0.65);
}

TEST_CASE("MultiShapeWavetable: setShape and setShapePosition equivalence", "[multiwt]")
{
    // setShape(Saw) must produce identical output to setShapePosition(2.0)
    // (Saw is index 2 in the enum).
    MultiShapeWavetable a, b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    a.setFrequency(440.0f);
    b.setFrequency(440.0f);
    a.setShape(MultiShapeWavetable::Shape::Saw);
    b.setShapePosition(static_cast<float>(MultiShapeWavetable::Shape::Saw));

    for (int i = 0; i < 1000; ++i)
    {
        float sa = a.process();
        float sb = b.process();
        REQUIRE_THAT(sa, WithinAbs(sb, 1e-6f));
    }
}

// ---------------------------------------------------------------------------
// Parameter boundary behaviour
// ---------------------------------------------------------------------------

TEST_CASE("MultiShapeWavetable: frequency above Nyquist clamps", "[multiwt]")
{
    // setFrequency clamps to sr*0.5 = 22050 — instance at 99999 must produce
    // same samples as instance at 22050.
    MultiShapeWavetable a, b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    a.setFrequency(22050.0f);
    b.setFrequency(99999.0f);

    for (int i = 0; i < 1000; ++i)
    {
        float sa = a.process();
        float sb = b.process();
        REQUIRE(sa == sb);
    }
}

TEST_CASE("MultiShapeWavetable: shape position clamps to [0, count-1]", "[multiwt]")
{
    MultiShapeWavetable a, b, c;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    c.prepare(kSampleRate);
    a.setFrequency(440.0f);
    b.setFrequency(440.0f);
    c.setFrequency(440.0f);

    a.setShapePosition(-5.0f);                                            // clamps to 0
    b.setShapePosition(0.0f);
    c.setShapePosition(static_cast<float>(MultiShapeWavetable::shapeCount() + 10)); // clamps to last

    MultiShapeWavetable last;
    last.prepare(kSampleRate);
    last.setFrequency(440.0f);
    last.setShapePosition(static_cast<float>(MultiShapeWavetable::shapeCount() - 1));

    for (int i = 0; i < 1000; ++i)
    {
        float sa = a.process();
        float sb = b.process();
        float sc = c.process();
        float sl = last.process();
        REQUIRE(sa == sb);
        REQUIRE(sc == sl);
    }
}

TEST_CASE("MultiShapeWavetable: frequency 0 Hz produces constant output", "[multiwt]")
{
    MultiShapeWavetable wt;
    wt.prepare(kSampleRate);
    wt.setShape(MultiShapeWavetable::Shape::Saw);
    wt.setFrequency(0.0f);

    float first = wt.process();
    for (int i = 1; i < 1000; ++i)
    {
        float s = wt.process();
        REQUIRE(s == first);
    }
}

// ---------------------------------------------------------------------------
// Long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("MultiShapeWavetable: 10-second stability across all shapes", "[multiwt]")
{
    const int n = static_cast<int>(MultiShapeWavetable::shapeCount());
    for (int s = 0; s < n; ++s)
    {
        MultiShapeWavetable wt;
        wt.prepare(kSampleRate);
        wt.setFrequency(440.0f);
        wt.setShape(static_cast<MultiShapeWavetable::Shape>(s));

        // 10 s at 44100 Hz = 441000 samples.  Catches phase-accumulator drift
        // (must stay in [0, 1) after wrap) and denormal accumulation (not
        // applicable here — no feedback state — but the bound check guards it).
        constexpr int N = 441000;
        for (int i = 0; i < N; ++i)
        {
            float v = wt.process();
            REQUIRE(v >= -1.05f);
            REQUIRE(v <= 1.05f);
        }
        REQUIRE(wt.getPhase() >= 0.0f);
        REQUIRE(wt.getPhase() < 1.0f);
    }
}
