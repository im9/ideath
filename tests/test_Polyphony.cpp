#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Polyphony.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// Polyphony::process sums all active voices and applies tanh soft
// saturation:
//     mix = Σ voice_i.process()
//     out = tanh(mix)
//
// Each Voice (at default source = saw, velocity = 1, env = 1) outputs a
// band-limited saw in [-1, +1] with theoretical RMS = 1/√3 ≈ 0.577 (see
// test_Voice.cpp derivation header).
//
// tanh-compression of a single saw voice:
//     <tanh²(saw)>_saw∼U[-1,1] = (1/2)·∫_{-1}^{1} tanh²(x) dx
//                             = ∫₀¹ tanh²(x) dx = 1 − tanh(1)
//                             ≈ 1 − 0.7616 = 0.2384
//     RMS_single ≈ √0.2384 ≈ 0.488.
//
// Chord (N independent saws summed, then tanhed):
//   By CLT the sum → Gaussian with σ = √(N/3).  For N=3: σ = 1.
//   E[tanh²(N(0,1))] ≈ 0.4 (numerical; between linear x² and saturated 1).
//   RMS_chord(N=3) ≈ 0.63.  Ratio RMS_chord / RMS_single ≈ 1.29.
//
// tanh rail detection:
//   tanh(x) rounds to IEEE single-precision 1.0f only when
//       1 − tanh(x) < ULP(1)/2 ≈ 6e−8/2 = 3e−8,
//   i.e. |x| > arctanh(1 − 3e−8) ≈ 8.7.  An 8-voice chord with
//   detuned pitches has pre-tanh peak magnitude ≤ 8 (all saws at
//   ±1 simultaneously) but that worst case never aligns across
//   a 2-octave spread within a few-thousand-sample window.
//   Therefore a correctly-configured tanh saturator produces
//   exactRailHits == 0 on this test.

static float rms(const float* buf, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

TEST_CASE("Polyphony: silent with no notes", "[poly]")
{
    // No active voices → mix = 0 → tanh(0) = 0.0f bit-exactly.
    // Tolerance 1e−9 is a no-op for the float-matcher API.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);

    for (int i = 0; i < 1000; ++i)
        REQUIRE_THAT(poly.process(), WithinAbs(0.0f, 1e-9f));

    REQUIRE(poly.getActiveVoiceCount() == 0);
    REQUIRE_FALSE(poly.hasActiveVoices());
}

TEST_CASE("Polyphony: single note produces sound", "[poly]")
{
    // Single-voice output = tanh(saw · env · velocity).  With velocity
    // and envelope at 1 after the 1 ms attack, the derivation above
    // gives RMS_single ≈ 0.488.  Over N = 4410 samples (0.1 s), the
    // 44-sample attack ramp contributes a small additional attenuation
    // (< 1 % correction), so
    //     RMS ≈ 0.488 · √(4366/4410) ≈ 0.486.
    // Threshold > 0.4 gives ~20 % headroom and catches a regression
    // that drops voice output or breaks the source wiring.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    poly.noteOn(440.0f);
    REQUIRE(poly.getActiveVoiceCount() == 1);

    constexpr int N = 4410;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = poly.process();

    REQUIRE(rms(buf.data(), N) > 0.4f);
}

TEST_CASE("Polyphony: multiple notes stack", "[poly]")
{
    // Binary voice-count check.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    poly.noteOn(261.63f); // C4
    poly.noteOn(329.63f); // E4
    poly.noteOn(392.00f); // G4

    REQUIRE(poly.getActiveVoiceCount() == 3);
    REQUIRE(poly.hasActiveVoices());
}

TEST_CASE("Polyphony: noteOff releases matching voice", "[poly]")
{
    // Binary: after noteOff + release time, only one voice remains.
    // Release = 5 ms → reaches 1e−5 flush threshold in ~10·τ = 50 ms =
    // 2205 samples (see test_Envelope.cpp).  1 s (44100 samples) is
    // 20× that, more than enough.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);
    poly.setRelease(0.005f);

    poly.noteOn(440.0f);
    poly.noteOn(880.0f);
    REQUIRE(poly.getActiveVoiceCount() == 2);

    poly.noteOff(440.0f);
    REQUIRE(poly.getActiveVoiceCount() == 2); // still active during release

    for (int i = 0; i < 44100; ++i)
        poly.process();

    REQUIRE(poly.getActiveVoiceCount() == 1); // 880 still sustaining
}

TEST_CASE("Polyphony: allNotesOff releases everything", "[poly]")
{
    // Binary: same release-time logic as the single-note case.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 4);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);
    poly.setRelease(0.005f);

    poly.noteOn(261.63f);
    poly.noteOn(329.63f);
    poly.noteOn(392.00f);
    REQUIRE(poly.getActiveVoiceCount() == 3);

    poly.allNotesOff();

    for (int i = 0; i < 44100; ++i)
        poly.process();

    REQUIRE(poly.getActiveVoiceCount() == 0);
}

TEST_CASE("Polyphony: voice stealing when pool exhausted", "[poly]")
{
    // Binary count: a 5th noteOn with a full 4-voice pool must reuse
    // an existing slot rather than allocating (Polyphony is real-time
    // safe — no growth after prepare()).
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 4);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    poly.noteOn(261.63f);
    poly.noteOn(329.63f);
    poly.noteOn(392.00f);
    poly.noteOn(523.25f);
    REQUIRE(poly.getActiveVoiceCount() == 4);

    poly.noteOn(659.25f);
    REQUIRE(poly.getActiveVoiceCount() == 4);
}

TEST_CASE("Polyphony: voice stealing takes the oldest note", "[poly]")
{
    // Contract (src/Polyphony.cpp::findVoiceForNote): when the pool is
    // full, steal the voice with the smallest voiceAge_.  That is the
    // oldest allocation, set at noteOn time via ++ageCounter_.
    //
    // Protocol to verify:
    //   1. Fill pool with 4 distinct pitches (ages 1,2,3,4).
    //   2. noteOn a 5th pitch → steals the age-1 voice.
    //   3. noteOff on the 1st (stolen) pitch is a no-op: the voice
    //      that had freq=1st-pitch is gone, so noteOff matches nothing.
    //      Active count is unchanged after release time elapses.
    //   4. noteOff on the 5th pitch releases that voice — count drops
    //      by one.
    //   5. noteOff on the 2nd, 3rd, 4th pitches releases them too;
    //      after release time the count is 0.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 4);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);
    poly.setRelease(0.005f);

    const float f1 = 200.0f, f2 = 300.0f, f3 = 400.0f, f4 = 500.0f;
    const float f5 = 600.0f;

    poly.noteOn(f1); // age 1
    poly.noteOn(f2);
    poly.noteOn(f3);
    poly.noteOn(f4);
    REQUIRE(poly.getActiveVoiceCount() == 4);

    poly.noteOn(f5); // should steal the f1 voice (oldest)
    REQUIRE(poly.getActiveVoiceCount() == 4);

    // noteOff(f1) should match nothing — the f1 voice was stolen.
    poly.noteOff(f1);
    for (int i = 0; i < 44100; ++i) poly.process();
    REQUIRE(poly.getActiveVoiceCount() == 4);

    // noteOff(f5) releases the stealer.
    poly.noteOff(f5);
    for (int i = 0; i < 44100; ++i) poly.process();
    REQUIRE(poly.getActiveVoiceCount() == 3);

    // Release the survivors.
    poly.noteOff(f2);
    poly.noteOff(f3);
    poly.noteOff(f4);
    for (int i = 0; i < 44100; ++i) poly.process();
    REQUIRE(poly.getActiveVoiceCount() == 0);
}

TEST_CASE("Polyphony: output is in [-1, 1] range", "[poly]")
{
    // tanh(x) ∈ (−1, +1) strictly for finite x, so any Polyphony output
    // is bit-exactly bounded by ±1 (and in practice never reaches ±1.0f
    // — see the heavy-chord test below for the float-rail derivation).
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    poly.noteOn(261.63f, 0.5f);
    poly.noteOn(329.63f, 0.5f);
    poly.noteOn(392.00f, 0.5f);

    constexpr int N = 4410;
    for (int i = 0; i < N; ++i)
    {
        float s = poly.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("Polyphony: heavy chord saturates softly without flat-topping", "[poly]")
{
    // Regression: the mix bus used to hard-clip the raw voice sum to
    // [-1, 1], producing flat-topped output (many consecutive samples
    // pinned at exactly ±1) and harsh harmonics.  After switching to
    // tanh the output should stay bounded but never sit at the rail.
    //
    // Derivation of the exact-rail threshold:
    //   IEEE float32 ULP at 1.0 is 2⁻²³ ≈ 1.19e−7.  tanh(x) rounds to
    //   the literal value 1.0f when 1 − tanh(x) < ULP/2 ≈ 6e−8, i.e.
    //   when |x| > arctanh(1 − 6e−8) ≈ 8.7.  The 8-voice chord used
    //   here spans ~2 octaves so the per-sample sum peaks ≈ 5–6 in
    //   practice (the worst-case aligned peak of ±8 is vanishingly
    //   rare on any finite window at detuned pitches).  Therefore
    //   exactRailHits must be 0 for the soft-saturator implementation.
    //
    // Derivation of the flat-run threshold:
    //   Hard clip flattens consecutive samples whenever the pre-clip
    //   input stays outside [−1, 1] — tens to hundreds of consecutive
    //   bit-identical floats per saturation event.  tanh's derivative
    //   is 1 − tanh²(x) > 0 for all finite x, so consecutive identical
    //   floats can only occur by float-rounding coincidence on a pair
    //   of very close pre-tanh values.  A handful of such coincidences
    //   is expected; runs > ~4 are not.  Threshold < 4 rejects the
    //   hard-clip regression with room to spare.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    const float freqs[8] = { 130.81f, 164.81f, 196.00f, 246.94f,
                             261.63f, 329.63f, 392.00f, 493.88f };
    for (float f : freqs)
        poly.noteOn(f, 1.0f);

    for (int i = 0; i < 2048; ++i)
        poly.process();

    constexpr int N = 8192;
    int exactRailHits = 0;
    int flatRunPairs = 0;
    float prev = poly.process();
    for (int i = 1; i < N; ++i)
    {
        float s = poly.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
        if (s == 1.0f || s == -1.0f)
            ++exactRailHits;
        if (s == prev)
            ++flatRunPairs;
        prev = s;
    }
    INFO("exact rail hits: " << exactRailHits
         << "  flat-run pairs: " << flatRunPairs << " / " << N);
    REQUIRE(exactRailHits == 0);
    REQUIRE(flatRunPairs < 4);
}

TEST_CASE("Polyphony: reset clears all voices", "[poly]")
{
    // Binary + bit-exact: reset() calls Voice::reset() on every pool
    // entry and clears voiceAge_, so the next process() returns
    // tanh(0) = 0.0f exactly.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setSustain(1.0f);

    poly.noteOn(440.0f);
    poly.noteOn(880.0f);
    REQUIRE(poly.getActiveVoiceCount() == 2);

    poly.reset();
    REQUIRE(poly.getActiveVoiceCount() == 0);
    REQUIRE_THAT(poly.process(), WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("Polyphony: chord is louder than single note", "[poly]")
{
    // Single voice: RMS_single ≈ 0.488 (see header derivation).
    // Chord (3 uncorrelated saws at C-E-G, summed then tanhed):
    // pre-tanh sum is ≈ Gaussian N(0, 1) (CLT, σ = √(3/3) = 1), and
    // E[tanh²(N(0,1))] ≈ 0.4 numerically → RMS_chord ≈ 0.63.
    // Expected ratio RMS_chord / RMS_single ≈ 1.29.
    //
    // Threshold > 1.1 gives ~17 % margin over the predicted ratio and
    // rejects any regression that silences or mis-phases the extra
    // voices.  (Cannot be bit-exact because the three pitches are
    // mutually incommensurate so there is always some integration-
    // window-dependent variance in RMS measurements.)
    ideath::Polyphony single;
    single.prepare(kSampleRate, 8);
    single.setAttack(0.001f);
    single.setSustain(1.0f);
    single.noteOn(440.0f);

    ideath::Polyphony chord;
    chord.prepare(kSampleRate, 8);
    chord.setAttack(0.001f);
    chord.setSustain(1.0f);
    chord.noteOn(261.63f);
    chord.noteOn(329.63f);
    chord.noteOn(392.00f);

    constexpr int N = 4410;
    std::vector<float> bufS(N), bufC(N);
    for (int i = 0; i < N; ++i)
    {
        bufS[static_cast<size_t>(i)] = single.process();
        bufC[static_cast<size_t>(i)] = chord.process();
    }

    const float rS = rms(bufS.data(), N);
    const float rC = rms(bufC.data(), N);
    REQUIRE(rS > 0.0f);
    REQUIRE(rC > rS * 1.1f);
}

TEST_CASE("Polyphony: getMaxVoices returns configured count", "[poly]")
{
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 12);
    REQUIRE(poly.getMaxVoices() == 12);
}

TEST_CASE("Polyphony: 10-second stability under sustained chord", "[poly]")
{
    // CLAUDE.md convention: primitives with feedback/phase accumulators
    // require ≥ 10 s continuous processing to catch precision drift and
    // denormal accumulation.  Polyphony holds N × Voice state (each
    // with oscillator phase, filter state, envelope state), so drift
    // here compounds across voices.
    //
    // Sustain a 4-voice chord with a resonant filter for 10 s; verify
    // every sample stays finite and within tanh's (−1, +1) bound.  Use
    // batched bool flags to keep the test fast.
    ideath::Polyphony poly;
    poly.prepare(kSampleRate, 8);
    poly.setAttack(0.001f);
    poly.setDecay(0.5f);
    poly.setSustain(0.8f);
    poly.setRelease(0.1f);
    poly.setFilter(ideath::Voice::FilterType::Lowpass, 1200.0f, 2.0f);

    poly.noteOn(220.0f);
    poly.noteOn(329.63f);
    poly.noteOn(440.0f);
    poly.noteOn(659.25f);

    constexpr int N = 441000; // 10 s
    bool allFinite = true;
    bool allBounded = true;
    for (int i = 0; i < N; ++i)
    {
        const float s = poly.process();
        if (!std::isfinite(s)) { allFinite = false; break; }
        if (s < -1.0f || s > 1.0f) { allBounded = false; break; }
    }
    REQUIRE(allFinite);
    REQUIRE(allBounded);
}
