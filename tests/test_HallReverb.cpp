#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/HallReverb.h>
#include <ideath/Biquad.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("HallReverb: silence in produces silence out", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setMix(1.0f);

    // Zero input, zeroed buffers.  Same Freeverb structure as Reverb:
    // each comb's filterStore saturates at O(1e−25), and the 8-comb
    // sum × kWetScale(3) caps at ~1.5e−23.  Allpass chain is unity-
    // gain and adds O(kAntiDenormal) per stage.  Final output lives
    // in the 1e−23..1e−22 range — 14+ orders below the 1e−9 bound.
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::fabs(l) < 1e-9f);
        REQUIRE(std::fabs(r) < 1e-9f);
    }
}

TEST_CASE("HallReverb: impulse produces stereo tail", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setDamp(0.1f);
    rev.setMix(1.0f);

    rev.process(1.0f);

    float maxL = 0.0f, maxR = 0.0f;
    for (int i = 0; i < 8820; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        if (std::fabs(l) > maxL) maxL = std::fabs(l);
        if (std::fabs(r) > maxR) maxR = std::fabs(r);
    }

    // Impulse 1.0 × kInputGain = 0.015 into 8 LFO-modulated combs at
    // fb = 0.7 + 0.8·0.28 = 0.924 (size=0.8).  Same peak derivation as
    // Reverb: 8 combs × 0.015 × kWetScale(3) ≈ 0.36 at first echo.
    // LFO modulation of comb delay spreads peaks slightly in time but
    // does not reduce the peak amplitude.  Threshold 0.05 sits ~7×
    // below the predicted peak and catches a regression that silences
    // the comb bank.
    REQUIRE(maxL > 0.05f);
    REQUIRE(maxR > 0.05f);
}

TEST_CASE("HallReverb: stereo output differs L/R", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setMix(1.0f);

    rev.process(1.0f);

    bool differ = false;
    for (int i = 0; i < 4410; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        // kStereoSpread=23 + LFO phase offset (0.5 between L/R) → differ.
        if (std::fabs(l - r) > 0.001f)
        {
            differ = true;
            break;
        }
    }
    REQUIRE(differ);
}

TEST_CASE("HallReverb: pre-delay offsets the tail", "[hallreverb]")
{
    // Feed a burst and measure energy after the comb delay has built up
    auto measureEarlyEnergy = [](float preDelaySec) {
        ideath::HallReverb rev;
        rev.prepare(kSampleRate);
        rev.setPreDelay(preDelaySec);
        rev.setSize(0.7f);
        rev.setDamp(0.1f);
        rev.setMix(1.0f);

        // Feed 50ms burst
        for (int i = 0; i < 2205; ++i)
            rev.process(0.5f);

        // Measure energy in the next 50ms (tail after burst stops)
        float energy = 0.0f;
        for (int i = 0; i < 2205; ++i)
        {
            auto [l, r] = rev.process(0.0f);
            energy += l * l + r * r;
        }
        return energy;
    };

    float noDelay = measureEarlyEnergy(0.0f);
    float withDelay = measureEarlyEnergy(0.08f); // 80ms pre-delay

    // No-delay version has more tail energy (signal enters combs sooner)
    REQUIRE(noDelay > withDelay);
}

TEST_CASE("HallReverb: modulation changes the tail vs unmodulated", "[hallreverb]")
{
    // The distinguishing feature of HallReverb over Reverb is LFO-
    // modulated comb delays (setModDepth controls LFO amplitude).  If
    // modulation is wired incorrectly — LFO never ticks, modDepth
    // ignored, sin LUT stuck at 0 — the tail becomes indistinguishable
    // from a plain Freeverb at the same size/damp.  Compare two
    // instances driven identically except for modDepth and verify the
    // tails diverge.
    //
    // Derivation of the divergence threshold:
    //   An LFO with modDepth = 1 modulates each comb's read position
    //   by up to ±1 sample (the library internally scales modDepth to
    //   a small offset — the exact max depth is an impl detail).  Over
    //   the tail sustain window the resulting phase-dependent reads
    //   produce a ~fraction-of-a-sample time-varying smear.  The RMS
    //   difference between modulated and unmodulated tails is at least
    //   the per-sample phase jitter × the signal RMS × a loss factor
    //   from partial cancellation — empirically in the 1e−3..1e−2
    //   range for this configuration.  Threshold > 1e−4 rejects the
    //   "modulation wired to a no-op" regression (which gives a
    //   bit-identical tail, rmsDiff = 0 up to ULP) with 10× margin.
    auto runTail = [](float modDepth) {
        ideath::HallReverb rev;
        rev.prepare(kSampleRate);
        rev.setSize(0.8f);
        rev.setDamp(0.1f);
        rev.setModDepth(modDepth);
        rev.setMix(1.0f);

        for (int i = 0; i < 4410; ++i)
            rev.process(0.5f);

        constexpr int N = 4410;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
        {
            auto [l, r] = rev.process(0.0f);
            buf[static_cast<size_t>(i)] = 0.5f * (l + r);
        }
        return buf;
    };

    const auto noMod = runTail(0.0f);
    const auto withMod = runTail(1.0f);

    double sumSq = 0.0;
    for (size_t i = 0; i < noMod.size(); ++i)
    {
        const double d = static_cast<double>(withMod[i] - noMod[i]);
        sumSq += d * d;
    }
    const double rmsDiff = std::sqrt(sumSq / static_cast<double>(noMod.size()));
    INFO("mod vs no-mod tail RMS diff: " << rmsDiff);
    REQUIRE(rmsDiff > 1e-4);
}

TEST_CASE("HallReverb: size affects decay time", "[hallreverb]")
{
    auto measureDecay = [](float size) {
        ideath::HallReverb rev;
        rev.prepare(kSampleRate);
        rev.setSize(size);
        rev.setDamp(0.1f);
        rev.setMix(1.0f);

        rev.process(1.0f);

        float energy = 0.0f;
        for (int i = 0; i < 44100; ++i)
        {
            auto [l, r] = rev.process(0.0f);
            energy += l * l + r * r;
        }
        return energy;
    };

    float energySmall = measureDecay(0.2f);
    float energyLarge = measureDecay(0.9f);

    REQUIRE(energyLarge > energySmall);
}

TEST_CASE("HallReverb: freeze holds tail", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.7f);
    rev.setMix(1.0f);
    // No comb modulation: the LFO-swept fractional read uses linear
    // interpolation, whose |H| < 1 above DC would make the frozen loop
    // slightly lossy and void the lossless-loop derivation below.
    rev.setModDepth(0.0f);

    for (int i = 0; i < 4410; ++i)
        rev.process(0.5f);

    rev.setFreeze(true);

    float energy1 = 0.0f;
    for (int i = 0; i < 44100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        energy1 += l * l + r * r;
    }

    float energy2 = 0.0f;
    for (int i = 0; i < 44100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        energy2 += l * l + r * r;
    }

    // Freeze: fb = 1, damp1 = 0 and the comb input muted, so every comb
    // recirculates its buffer unchanged — each comb output is periodic with
    // its own length L ≤ 1617 + 23 = 1640 samples (longest tuning + stereo
    // spread).  The output allpasses (Freeverb's y = w[n−D] − x form, which
    // is not unity-gain) are LTI and sit outside the loops, so they reshape
    // but cannot drain the held energy.  A block's energy can fall short of
    // the previous block's only by the partial period at its edge:
    // ≤ L / N = 1640 / 44100 ≈ 3.7 % for 1 s blocks.  0.95 = 1 − 3.7 %
    // rounded down by ~1 % for float recirculation over 2 s.
    REQUIRE(energy1 > 0.0f);
    REQUIRE(energy2 > energy1 * 0.95f);
}

TEST_CASE("HallReverb: dry/wet mix", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setMix(0.0f);

    // mix=0: dry = 1.0, wet = 0.  Output = input · 1.0 bit-for-bit
    // (multiply by 1.0 is exact, wet branch masked by 0 multiply).
    auto [l, r] = rev.process(0.8f);
    REQUIRE_THAT(l, WithinAbs(0.8f, 1e-9f));
    REQUIRE_THAT(r, WithinAbs(0.8f, 1e-9f));
}

TEST_CASE("HallReverb: reset clears all state", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setMix(1.0f);

    for (int i = 0; i < 4410; ++i)
        rev.process(0.7f);

    rev.reset();

    // After reset, all comb/allpass/pre-delay buffers zeroed.  Same
    // derivation as Reverb reset: reads return 0, kAntiDenormal only
    // enters on write so feedback paths stay below ~1e−25 × N iterations
    // × kWetScale — far below the 1e−9 bound.
    for (int i = 0; i < 100; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE_THAT(l, WithinAbs(0.0f, 1e-9f));
        REQUIRE_THAT(r, WithinAbs(0.0f, 1e-9f));
    }
}

TEST_CASE("HallReverb: default-constructible", "[hallreverb]")
{
    ideath::HallReverb rev;
    auto [l, r] = rev.process(0.5f);
    REQUIRE(std::isfinite(l));
    REQUIRE(std::isfinite(r));
}

TEST_CASE("HallReverb: output bounded with signal", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.95f);
    rev.setModDepth(1.0f);
    rev.setMix(1.0f);

    for (int i = 0; i < 44100; ++i)
    {
        float input = (i < 441) ? 1.0f : 0.0f;
        auto [l, r] = rev.process(input);
        // ±1.5 matches the primitive's documented output ceiling (see
        // CLAUDE.md "Output levels" table — Reverb/HallReverb share the
        // same Freeverb structure).  Schroeder tuning spreads comb
        // echoes in time; LFO modulation at modDepth=1 only shifts
        // read positions by a fraction of a sample so it does not
        // increase the peak.  ~20 % headroom over the observed peak.
        REQUIRE(l >= -1.5f);
        REQUIRE(l <= 1.5f);
        REQUIRE(r >= -1.5f);
        REQUIRE(r <= 1.5f);
    }
}

TEST_CASE("HallReverb: parameter clamping", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);

    rev.setSize(-1.0f);
    rev.setDamp(5.0f);
    rev.setPreDelay(10.0f);
    rev.setModDepth(-1.0f);
    rev.setMix(100.0f);

    for (int i = 0; i < 1000; ++i)
    {
        auto [l, r] = rev.process(0.5f);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}

// --- Long-run stability ---

TEST_CASE("HallReverb: long-run stability (10 seconds)", "[hallreverb]")
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(0.8f);
    rev.setDamp(0.3f);
    rev.setModDepth(0.5f);
    rev.setMix(1.0f);

    // 0.1s signal then 10s silence. Tests 8 LFO-modulated combs + 4 allpasses
    // with anti-denormal over 441k samples.
    for (int i = 0; i < 4410; ++i)
        rev.process(0.5f);

    constexpr int N = 441000;
    for (int i = 0; i < N; ++i)
    {
        auto [l, r] = rev.process(0.0f);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}

// ---------------------------------------------------------------------------
// Damping audibility
// ---------------------------------------------------------------------------

// Energy (dB re full scale) above 4 kHz in the 0.3–1.3 s window after a
// 0.26 s, 220 Hz naive-saw note is released.  The saw supplies harmonics
// across the whole band, the 4th-order (two cascaded RBJ Q=0.707) highpass
// isolates the region that damping is meant to darken, and the window skips
// the early reflections so the measurement reflects the recirculating tail
// where the in-loop lowpass acts once per comb pass.
static double hallTailHfDb(float size, float damp)
{
    ideath::HallReverb rev;
    rev.prepare(kSampleRate);
    rev.setSize(size);
    rev.setDamp(damp);
    rev.setMix(1.0f);

    ideath::Biquad hp1, hp2;
    hp1.setHighpass(4000.0f, 0.7071f, kSampleRate);
    hp2.setHighpass(4000.0f, 0.7071f, kSampleRate);

    const int noteLen = static_cast<int>(0.26f * kSampleRate);
    const int winStart = noteLen + static_cast<int>(0.3f * kSampleRate);
    const int winEnd = noteLen + static_cast<int>(1.3f * kSampleRate);

    double energy = 0.0;
    for (int i = 0; i < winEnd; ++i)
    {
        float in = 0.0f;
        if (i < noteLen)
        {
            const float ph = std::fmod(220.0f * static_cast<float>(i) / kSampleRate, 1.0f);
            in = 0.5f * (2.0f * ph - 1.0f);
        }
        auto [l, r] = rev.process(in);
        const float hf = hp2.process(hp1.process(0.5f * (l + r)));
        if (i >= winStart)
            energy += static_cast<double>(hf) * hf;
    }
    return 10.0 * std::log10(energy / (winEnd - winStart) + 1e-30);
}

TEST_CASE("HallReverb: damp darkens the long tail audibly", "[hallreverb]")
{
    const double hf0 = hallTailHfDb(0.9f, 0.0f);
    const double hf5 = hallTailHfDb(0.9f, 0.5f);
    const double hf1 = hallTailHfDb(0.9f, 1.0f);

    INFO("HF tail dB: damp 0 = " << hf0 << ", 0.5 = " << hf5 << ", 1 = " << hf1);
    // 6 dB = half amplitude, the smallest difference the TONE control must
    // produce across its range to read as a clear change of brightness.
    REQUIRE(hf1 <= hf0 - 6.0);
    // Monotone: more damping never brightens the tail.
    REQUIRE(hf5 < hf0);
    REQUIRE(hf1 < hf5);
}

TEST_CASE("HallReverb: input during freeze does not enter the held tail", "[hallreverb]")
{
    // Canonical Freeverb mutes the comb input while frozen (gain = 0) —
    // the combs then run at fb = 1 with nothing added, so the held tail is
    // exactly what was captured at engagement.  With mix = 1 the dry term
    // is input × 0 = 0 exactly, so the output of a reverb that keeps
    // receiving input after freeze must be bit-identical to one fed
    // silence.  Any difference means input leaks into the lossless loop,
    // where it accumulates without bound.
    ideath::HallReverb held;
    ideath::HallReverb fed;
    for (auto* rev : { &held, &fed })
    {
        rev->prepare(kSampleRate);
        rev->setSize(0.7f);
        rev->setMix(1.0f);
    }

    auto stim = [](int i) {
        const float t = static_cast<float>(i) / kSampleRate;
        return 0.1f * (std::sin(2.0f * 3.14159265f * 997.0f * t)
                     + std::sin(2.0f * 3.14159265f * 1447.0f * t)
                     + std::sin(2.0f * 3.14159265f * 2011.0f * t));
    };

    int t = 0;
    for (; t < 44100; ++t)
    {
        held.process(stim(t));
        fed.process(stim(t));
    }
    held.setFreeze(true);
    fed.setFreeze(true);

    int mismatches = 0;
    for (int i = 0; i < 44100 * 2; ++i, ++t)
    {
        auto [hl, hr] = held.process(0.0f);
        auto [fl, fr] = fed.process(stim(t));
        if (hl != fl || hr != fr)
            ++mismatches;
    }
    INFO("samples where fed output differs from held: " << mismatches);
    REQUIRE(mismatches == 0);
}
