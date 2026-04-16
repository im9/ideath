#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/FMSynth.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// FMSynth output pipeline (src/FMSynth.cpp::process):
//   per op: tick(modIn) = sin(phase*2π + modIn + feedback * prev * 3.2) * env * level
//   out    = (Σ carrier_out) / carrierCount * velocity * 0.85
//   out    = clamp(out, -1, +1)      // hard limit on the return
//
// Implementation constants treated as design contract:
//   0.85    — post-mix output gain (prevents clipping at typical velocity=1)
//   3.2     — feedback-to-mod-index multiplier on prevOut
//   sin     — carrier waveform (|out| ≤ 1 for any argument)
//
// Two consequences used repeatedly below:
//
// (a) Per-sample amplitude bound
//     |tick(·)| ≤ 1 (env, level clamped to [0,1]) ⇒ |Σ/N| ≤ 1 ⇒ |out| ≤ 0.85
//     at velocity=1 before the hard-limit step.  So the hard-limit is
//     bit-exact and unreached in normal operation.
//
// (b) RMS invariance under FM
//     FM only redistributes spectral content; Parseval gives
//     <sin²(any argument)> = 0.5 averaged over a full period of the
//     slowest component.  Therefore a single operator at full envelope
//     has RMS = 1/√2 regardless of modulation topology; scaled by 0.85
//     the whole-synth RMS ≈ 0.85/√2 ≈ 0.601 when the carriers evolve
//     coherently (same phaseInc, ratio=1, start at phase 0).
//
// AdsrEnvelope default state (relevant to several tests):
//   attackRate_ = 0 when setAttack() has NOT been called → envelope stays
//   at level 0 in Stage::Attack forever.  Tests that only configure op0's
//   envelope therefore get exactly zero contribution from ops 1..3
//   regardless of the ratio/level defaults (which are 1.0 and 1.0).

static float rms(const float* buf, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

TEST_CASE("FMSynth: output is in [-1, 1] range", "[fmsynth]")
{
    // Derivation: process() ends with `if (out > 1.0f) out = 1.0f; else
    // if (out < -1.0f) out = -1.0f;`.  The return value is therefore
    // bit-exactly within [-1, +1] (no tolerance needed).
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);
    fm.noteOn(440.0f);

    constexpr int N = 44100; // 1 second
    for (int i = 0; i < N; ++i)
    {
        float s = fm.process();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("FMSynth: produces sound after noteOn", "[fmsynth]")
{
    // Only op0 has attack configured (attackRate > 0).  Ops 1..3 keep
    // attackRate = 0 so their envelopes stay at level 0 and contribute 0.
    // Algo 0 (serial, OP4→OP3→OP2→OP1): op0 is the final carrier and
    // receives modIn = 0 (op1.tick outputs 0).  Therefore
    //     out(t) = 0.85 · env(t) · sin(2π · 440 · t),   carrierCount = 1
    //
    // Envelope: 1 ms attack = 44 samples linear ramp 0 → 1, then sustain=1.
    // RMS over N = 4410 samples (0.1 s, ~44 carrier cycles):
    //     RMS² = 0.85²/4410 · [ Σ_{i=0..43}(i/44)² · <sin²>
    //                         + Σ_{i=44..4409}        · <sin²> ]
    //          ≈ 0.7225/4410 · [ 7.05 + 4366 · 0.5 ] ≈ 0.359
    //     RMS  ≈ 0.599
    // Band [0.55, 0.65] allows ~±8 % for attack-ramp fraction drift and
    // the non-integer cycle count over 0.1 s.
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAttack(0, 0.001f);
    fm.setSustain(0, 1.0f);
    fm.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[static_cast<size_t>(i)] = fm.process();

    const float r = rms(buf.data(), N);
    REQUIRE(r > 0.55f);
    REQUIRE(r < 0.65f);
}

TEST_CASE("FMSynth: silent when idle", "[fmsynth]")
{
    // Derivation: no noteOn → all ops in Stage::Idle → FMSynth::process
    // returns literal 0.0f on the early-out branch.  Bit-exact; the
    // matcher tolerance is only to satisfy the float-equality API.
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);

    constexpr int N = 1000;
    for (int i = 0; i < N; ++i)
        REQUIRE_THAT(fm.process(), WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("FMSynth: noteOff triggers release", "[fmsynth]")
{
    // Binary behaviour check.  op0 release = 0.01 s → envelope reaches
    // the 1e-5 flush-to-zero threshold after ~10 · 0.01 s · sr = 4410
    // samples.  Ops 1..3 start at level 0 so their Release branch flushes
    // on the first tick.  After 1 second (100× release time) isActive()
    // must therefore be false.
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAttack(0, 0.001f);
    fm.setSustain(0, 1.0f);
    fm.setRelease(0, 0.01f);
    fm.noteOn(440.0f);

    for (int i = 0; i < 2000; ++i)
        fm.process();

    REQUIRE(fm.isActive());
    fm.noteOff();

    for (int i = 0; i < 44100; ++i)
        fm.process();

    REQUIRE_FALSE(fm.isActive());
}

TEST_CASE("FMSynth: reset returns to idle", "[fmsynth]")
{
    // Binary + bit-exact: reset() sets every op's stage to Idle; the
    // subsequent process() takes the early-out branch and returns
    // literal 0.0f.
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);
    fm.noteOn(440.0f);
    for (int i = 0; i < 100; ++i)
        fm.process();

    REQUIRE(fm.isActive());
    fm.reset();
    REQUIRE_FALSE(fm.isActive());
    REQUIRE_THAT(fm.process(), WithinAbs(0.0f, 1e-9f));
}

TEST_CASE("FMSynth: all 8 algorithms produce output", "[fmsynth]")
{
    // All ops: attack = 1 ms, sustain = 1, ratio = 1 (default), level = 1
    // (default).  Every envelope reaches 1; every phase evolves at the
    // same rate starting from 0, so carriers are coherent across ops.
    //
    // Worked examples (RMS at sustain, ignoring 1 % attack-ramp loss):
    //   algo 0 (serial):   o1 = sin(φ + mod)        ⇒ RMS = 0.85/√2 ≈ 0.6
    //   algo 5 (2 op stack + 2 carriers):
    //     sum/3 = (sin(φ+sin(φ)) + 2·sin(φ))/3
    //     <sum²>/9 = (0.5 + 2 + 4·J₀(1)·0.5)/9 ≈ 0.45  ⇒ RMS ≈ 0.85·0.67 ≈ 0.57
    //   algo 6 (shared modulator):
    //     sum/3 = sin(φ + sin(φ))                   ⇒ RMS = 0.85/√2 ≈ 0.6
    //   algo 7 (additive, all ratios 1):
    //     sum/4 = sin(φ)                            ⇒ RMS = 0.85/√2 ≈ 0.6
    //
    // All algos therefore land near 0.6.  Threshold > 0.3 (i.e. > half
    // the theoretical RMS) catches a silent algorithm or any regression
    // that collapses the signal by ≥ 6 dB; it is intentionally loose so
    // that the test does not hand-couple to topology-specific RMS.
    for (int algo = 0; algo < ideath::FMSynth::kNumAlgorithms; ++algo)
    {
        ideath::FMSynth fm;
        fm.prepare(kSampleRate);
        fm.setAlgorithm(algo);
        for (int op = 0; op < 4; ++op)
        {
            fm.setAttack(op, 0.001f);
            fm.setSustain(op, 1.0f);
        }
        fm.noteOn(440.0f);

        constexpr int N = 4410;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
            buf[static_cast<size_t>(i)] = fm.process();

        INFO("algo = " << algo);
        REQUIRE(rms(buf.data(), N) > 0.3f);
    }
}

TEST_CASE("FMSynth: every algorithm has a distinct routing", "[fmsynth]")
{
    // Regression: algorithms 5 and 6 used to be byte-identical (same
    // routing, different comments) — seen in commit history as the
    // FMSynth entry in the audit skill's bug-template table.
    //
    // Derivation of threshold:
    //   Byte-identical routings give rmsDiff = 0 modulo ULP (~1e-7 after
    //   4410-sample accumulation).
    //
    //   The smallest meaningful topology delta is adding one modulation
    //   path to one carrier.  That carrier's contribution changes from
    //   sin(kφ) to sin(kφ + M), and their RMS difference is
    //       RMS² = <(sin(kφ) − sin(kφ+M))²>
    //            = 1 − 2·<sin(kφ)·sin(kφ+M)>
    //            = 1 − 2·J₀(‖M‖)·<sin²(kφ)>
    //            = 1 − J₀(‖M‖).
    //   For M = sin(·) (unit mod index), J₀(1) ≈ 0.765, so RMS ≈ 0.484 on
    //   a single carrier, scaled by 0.85/N and summed over whichever
    //   carriers differ.  Even the closest pair (algo 5 vs 6, which
    //   differs on two of three carriers) has rmsDiff ≈ 0.85·2·0.484/3 ≈
    //   0.27 after accounting for partial cancellation.
    //
    //   Threshold 0.05 sits ~5 orders of magnitude above the ULP floor
    //   and ~5× below the worst-case distinct-topology delta: catches
    //   byte-identical duplicates and near-identical miswirings without
    //   flagging legitimately-close siblings.
    auto runAlgo = [](int algo) {
        ideath::FMSynth fm;
        fm.prepare(kSampleRate);
        fm.setAlgorithm(algo);
        for (int op = 0; op < 4; ++op)
        {
            fm.setAttack(op, 0.001f);
            fm.setDecay(op, 0.5f);
            fm.setSustain(op, 1.0f);
            fm.setRelease(op, 0.1f);
            fm.setRatio(op, 1.0f + static_cast<float>(op));
            fm.setLevel(op, 1.0f);
        }
        fm.noteOn(220.0f);

        constexpr int N = 4410;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
            buf[static_cast<size_t>(i)] = fm.process();
        return buf;
    };

    std::vector<std::vector<float>> outputs;
    for (int algo = 0; algo < ideath::FMSynth::kNumAlgorithms; ++algo)
        outputs.push_back(runAlgo(algo));

    auto rmsDiff = [](const std::vector<float>& a, const std::vector<float>& b) {
        double s = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double d = static_cast<double>(a[i] - b[i]);
            s += d * d;
        }
        return std::sqrt(s / static_cast<double>(a.size()));
    };

    for (int i = 0; i < ideath::FMSynth::kNumAlgorithms; ++i)
    {
        for (int j = i + 1; j < ideath::FMSynth::kNumAlgorithms; ++j)
        {
            const double d = rmsDiff(outputs[i], outputs[j]);
            INFO("rms diff between algo " << i << " and " << j << " = " << d);
            REQUIRE(d > 0.05);
        }
    }
}

TEST_CASE("FMSynth: modulator changes timbre", "[fmsynth]")
{
    // Algo 0 (serial OP4→OP3→OP2→OP1).
    //   clean: only op0's env active → out = 0.85 · env · sin(2π·440·t).
    //   mod:   all 4 envelopes active; op1 level = 0.8, ratio = 2 →
    //          op0 receives a non-zero modIn from op1, which itself is
    //          phase-modulated by op2 which is modulated by op3.
    //
    // At sustain the difference per sample is
    //     |clean − mod| = 0.85 · |sin(φ) − sin(φ + o2_mod)|
    //                   = 0.85 · 2 · |sin(o2_mod/2)| · |cos(φ + o2_mod/2)|
    // with o2_mod = 0.8 · sin(2φ + sin(φ + sin(φ))) ∈ [-0.8, 0.8].
    //
    //   <|sin(o2_mod/2)|> ≈ 0.4·(2/π) ≈ 0.255   (sin-distributed arg)
    //   <|cos(φ + o2_mod/2)|> ≈ 2/π ≈ 0.637     (uniform-in-phase)
    //   <|diff|> ≈ 0.85 · 2 · 0.255 · 0.637 ≈ 0.28.
    //
    // Threshold 0.1 gives ~2.8× headroom over the computed mean |diff|
    // and catches a regression that breaks the modulation wiring.
    ideath::FMSynth clean;
    clean.prepare(kSampleRate);
    clean.setAlgorithm(0);
    clean.setAttack(0, 0.001f);
    clean.setSustain(0, 1.0f);
    clean.setLevel(1, 0.0f);
    clean.setLevel(2, 0.0f);
    clean.setLevel(3, 0.0f);
    clean.noteOn(440.0f);

    ideath::FMSynth modulated;
    modulated.prepare(kSampleRate);
    modulated.setAlgorithm(0);
    for (int op = 0; op < 4; ++op)
    {
        modulated.setAttack(op, 0.001f);
        modulated.setSustain(op, 1.0f);
    }
    modulated.setLevel(1, 0.8f);
    modulated.setRatio(1, 2.0f);
    modulated.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> bufClean(N), bufMod(N);
    for (int i = 0; i < N; ++i)
    {
        bufClean[static_cast<size_t>(i)] = clean.process();
        bufMod[static_cast<size_t>(i)] = modulated.process();
    }

    float diff = 0.0f;
    for (int i = 0; i < N; ++i)
        diff += std::abs(bufClean[static_cast<size_t>(i)] - bufMod[static_cast<size_t>(i)]);
    REQUIRE(diff / static_cast<float>(N) > 0.1f);
}

TEST_CASE("FMSynth: feedback adds harmonics", "[fmsynth]")
{
    // Single carrier (algo 0, only op0 env active) with/without self-
    // feedback.  The feedback factor is feedback · 3.2, so 0.5 → 1.6 mod
    // index applied to prev_out (itself ≈ 0.85·sin(·)).
    //
    //     noFb:   out = 0.85 · env · sin(2π·440·t)
    //     withFb: out = 0.85 · env · sin(2π·440·t + 1.6 · prev)
    //
    // Using the same sin-minus-sin identity as the modulator test with
    // Δ = 1.6 · prev ∈ [-1.36, 1.36]:
    //     <|sin(Δ/2)|> ≈ RMS(sin(0.8·sin(·))) · √(2/π)
    //                 = √((1−J₀(1.6))/2) · √(2/π)
    //                 = √(0.2725) · √(2/π) ≈ 0.41
    //     <|cos(φ+Δ/2)|> ≈ 2/π ≈ 0.637
    //     <|diff|> ≈ 0.85 · 2 · 0.41 · 0.637 ≈ 0.44.
    //
    // Threshold 0.1 gives ~4× headroom and catches a regression in the
    // feedback path.
    ideath::FMSynth noFb;
    noFb.prepare(kSampleRate);
    noFb.setAttack(0, 0.001f);
    noFb.setSustain(0, 1.0f);
    noFb.setFeedback(0, 0.0f);
    noFb.noteOn(440.0f);

    ideath::FMSynth withFb;
    withFb.prepare(kSampleRate);
    withFb.setAttack(0, 0.001f);
    withFb.setSustain(0, 1.0f);
    withFb.setFeedback(0, 0.5f);
    withFb.noteOn(440.0f);

    constexpr int N = 4410;
    std::vector<float> bufNoFb(N), bufFb(N);
    for (int i = 0; i < N; ++i)
    {
        bufNoFb[static_cast<size_t>(i)] = noFb.process();
        bufFb[static_cast<size_t>(i)] = withFb.process();
    }

    float diff = 0.0f;
    for (int i = 0; i < N; ++i)
        diff += std::abs(bufNoFb[static_cast<size_t>(i)] - bufFb[static_cast<size_t>(i)]);
    REQUIRE(diff / static_cast<float>(N) > 0.1f);
}

TEST_CASE("FMSynth: velocity scales output", "[fmsynth]")
{
    // Velocity is applied AFTER mixing (process(): out *= velocity_ * 0.85).
    // Both instances share identical envelope trajectories, phase
    // increments, and starting phase, so
    //     sample_loud[i]  = (1.0 / 0.3) · sample_quiet[i]    bit-for-bit
    //     RMS_loud / RMS_quiet = 10 / 3 ≈ 3.333
    // up to the ULP of float multiplication on the post-mix gain.
    //
    // Tolerance 1e-4 is generous for ULP accumulated across 4410 RMS
    // samples (one rounding per sample, ≈ √N · ULP ≈ 7e-6 relative).
    ideath::FMSynth loud;
    loud.prepare(kSampleRate);
    loud.setAttack(0, 0.001f);
    loud.setSustain(0, 1.0f);
    loud.noteOn(440.0f, 1.0f);

    ideath::FMSynth quiet;
    quiet.prepare(kSampleRate);
    quiet.setAttack(0, 0.001f);
    quiet.setSustain(0, 1.0f);
    quiet.noteOn(440.0f, 0.3f);

    constexpr int N = 4410;
    std::vector<float> bufL(N), bufQ(N);
    for (int i = 0; i < N; ++i)
    {
        bufL[static_cast<size_t>(i)] = loud.process();
        bufQ[static_cast<size_t>(i)] = quiet.process();
    }

    const float rL = rms(bufL.data(), N);
    const float rQ = rms(bufQ.data(), N);
    REQUIRE(rQ > 0.0f);
    REQUIRE_THAT(rL / rQ, WithinAbs(10.0f / 3.0f, 1e-4f));
}

TEST_CASE("FMSynth: ratio changes pitch", "[fmsynth]")
{
    // Algo 7 additive.  Only op0 has non-zero level; ops 1..3 output 0
    // (level = 0).  Output:
    //     out = (sin(2π·440·ratio·t) · env · 1 + 0 + 0 + 0) / 4 · 1 · 0.85
    //         = 0.2125 · env · sin(2π·440·ratio·t).
    //
    // After skipping 200 samples the envelope is at sustain = 1, so the
    // signal is a clean 0.2125-amplitude sinusoid at f = 440·ratio Hz.
    //
    // Zero crossings over Δt = 4410 / 44100 s = 0.1 s:
    //     crossings(ratio) = 2 · 440 · ratio · 0.1 = 88 · ratio
    //       ratio = 1 → 88 crossings
    //       ratio = 2 → 176 crossings
    //
    // Sample-level sign-change detection has ±1 boundary uncertainty
    // (half-cycle = 50 samples at 880 Hz, so each transition is
    // unambiguously bracketed).  Accepted bands [86, 90] and [174, 178]
    // reflect this ±2-sample tolerance.  The ratio check [1.95, 2.05]
    // worst-case is (175/89, 177/87) = (1.966, 2.034).
    auto countCrossings = [](float ratio)
    {
        ideath::FMSynth fm;
        fm.prepare(kSampleRate);
        fm.setAlgorithm(7);
        fm.setLevel(0, 1.0f);
        fm.setLevel(1, 0.0f);
        fm.setLevel(2, 0.0f);
        fm.setLevel(3, 0.0f);
        fm.setRatio(0, ratio);
        fm.setAttack(0, 0.001f);
        fm.setSustain(0, 1.0f);
        fm.noteOn(440.0f);

        for (int i = 0; i < 200; ++i) fm.process();

        int crossings = 0;
        float prev = fm.process();
        constexpr int N = 4410;
        for (int i = 0; i < N; ++i)
        {
            float s = fm.process();
            if ((prev >= 0.0f && s < 0.0f) || (prev < 0.0f && s >= 0.0f))
                ++crossings;
            prev = s;
        }
        return crossings;
    };

    const int cross1 = countCrossings(1.0f);
    const int cross2 = countCrossings(2.0f);

    REQUIRE(cross1 >= 86);
    REQUIRE(cross1 <= 90);
    REQUIRE(cross2 >= 174);
    REQUIRE(cross2 <= 178);
    REQUIRE(cross2 > cross1 * 1.95);
    REQUIRE(cross2 < cross1 * 2.05);
}

TEST_CASE("FMSynth: algorithm clamps to valid range", "[fmsynth]")
{
    // Binary clamp check: setAlgorithm(std::max(0, std::min(algo, 7))).
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);

    fm.setAlgorithm(-1);
    REQUIRE(fm.getAlgorithm() == 0);

    fm.setAlgorithm(99);
    REQUIRE(fm.getAlgorithm() == 7);
}

TEST_CASE("FMSynth: operator index out of range is safe", "[fmsynth]")
{
    // Contract: out-of-range op index is a no-op, not a crash.  All
    // per-op setters guard `if (op < 0 || op >= kNumOperators) return;`.
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);

    fm.setRatio(-1, 2.0f);
    fm.setRatio(4, 2.0f);
    fm.setLevel(99, 0.5f);
    fm.setFeedback(-1, 0.3f);
    fm.setAttack(4, 0.1f);
}

TEST_CASE("FMSynth: 10-second stability across all algorithms", "[fmsynth]")
{
    // CLAUDE.md testing convention: "primitives with feedback or phase
    // accumulators must be tested for at least 10 seconds of continuous
    // processing to catch precision drift and denormal accumulation".
    // FMSynth has both: one phase accumulator per operator AND per-op
    // self-feedback.
    //
    // Drive all 8 algorithms at hot settings (max feedback, varied
    // ratios, full levels) and verify every sample stays finite and
    // within the hard-limit bound.  Use batched bool flags rather than
    // per-sample REQUIRE to keep the test fast.
    for (int algo = 0; algo < ideath::FMSynth::kNumAlgorithms; ++algo)
    {
        ideath::FMSynth fm;
        fm.prepare(kSampleRate);
        fm.setAlgorithm(algo);
        for (int op = 0; op < 4; ++op)
        {
            fm.setAttack(op, 0.001f);
            fm.setDecay(op, 0.2f);
            fm.setSustain(op, 1.0f);
            fm.setRelease(op, 0.1f);
            fm.setLevel(op, 1.0f);
            fm.setFeedback(op, 1.0f);
            fm.setRatio(op, 1.0f + static_cast<float>(op));
        }
        fm.noteOn(220.0f);

        constexpr int N = 441000; // 10 s at 44.1 kHz
        bool allFinite = true;
        bool allBounded = true;
        for (int i = 0; i < N; ++i)
        {
            const float s = fm.process();
            if (!std::isfinite(s)) { allFinite = false; break; }
            if (s < -1.0f || s > 1.0f) { allBounded = false; break; }
        }
        INFO("algo = " << algo);
        REQUIRE(allFinite);
        REQUIRE(allBounded);
    }
}

TEST_CASE("FMSynth: extreme parameter combination stays bounded", "[fmsynth]")
{
    // CLAUDE.md: "test pairs or triples of extreme parameters together".
    // Combine max feedback × max level × extreme ratio spread (min 0.5
    // and max 16.0 alternating) × serial-modulation algorithm (deepest
    // modulation chain).  sin(·) is bounded ±1 per op and the hard limit
    // guarantees the final output stays in [-1, +1] even if the internal
    // FM spectrum is chaotic.
    ideath::FMSynth fm;
    fm.prepare(kSampleRate);
    fm.setAlgorithm(0); // serial chain
    for (int op = 0; op < 4; ++op)
    {
        fm.setAttack(op, 0.001f);
        fm.setSustain(op, 1.0f);
        fm.setLevel(op, 1.0f);
        fm.setFeedback(op, 1.0f);
    }
    fm.setRatio(0, 16.0f);
    fm.setRatio(1, 0.5f);
    fm.setRatio(2, 16.0f);
    fm.setRatio(3, 0.5f);
    fm.noteOn(880.0f, 1.0f);

    constexpr int N = 44100; // 1 s
    bool allFinite = true;
    bool allBounded = true;
    for (int i = 0; i < N; ++i)
    {
        const float s = fm.process();
        if (!std::isfinite(s)) { allFinite = false; break; }
        if (s < -1.0f || s > 1.0f) { allBounded = false; break; }
    }
    REQUIRE(allFinite);
    REQUIRE(allBounded);
}
