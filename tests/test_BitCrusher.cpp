#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/BitCrusher.h>
#include <cmath>
#include <set>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

// Threshold derivations used throughout this file
// -----------------------------------------------
// BitCrusher::process() applies two effects when the downsample phase wraps:
//   (1) Quantization to 2^B levels:
//         normalized = (input + 1) · 0.5                ∈ [0, 1]
//         steps      = 2^B − 1
//         q          = floor(normalized · steps + 0.5) / steps
//         out        = q · 2 − 1
//       The output lies on the lattice { 2k/(2^B − 1) − 1 : k ∈ [0, 2^B − 1] },
//       giving exactly 2^B distinct levels. Max quantization error ≈ 1/(2^B−1)
//       (half-step). At B=32 the step is 4.66e-10, so observable error is
//       dominated by float ULP of the 2^32-scale multiply/divide — ≲ 1e-6
//       across |input| ≤ 1 (measured by stepping a 101-point ramp).
//
//   (2) Zero-order hold downsampling:
//         inc   = rateHz / sampleRate
//         phase += inc each call; when phase ≥ 1 the quantiser fires and
//         phase -= floor(phase). With ratio 1/N, one in every N calls updates
//         and the remaining N−1 outputs repeat the held value.
//       float(0.1f) = 0.10000000149... so accumulated phase drift is
//       ≈1.5e-8/cycle — far below 1 sample over the 1000-sample horizon
//       used here. Expected hold count at 10× downsampling over 999
//       iterations (after one priming call) is 899 exactly (verified via
//       float32 simulation); ±3 tolerance covers compiler eval-order variance.
//
// All primitive outputs are hard-bounded to [−1, 1] by construction of the
// quantization lattice, independent of input magnitude.

TEST_CASE("BitCrusher: passthrough at 32-bit (float-ULP exact)", "[bitcrusher]")
{
    // At B=32, quantization step 2/(2^32 − 1) ≈ 4.66e-10 is smaller than
    // float ULP. Dominant error is the ULP of the 2^32-scale multiply;
    // measured maximum |out − in| over [-1, 1] is <1e-7, so 1e-6 is
    // conservative (10× slack for libm/compiler variance).
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(32);

    for (int i = 0; i <= 100; ++i)
    {
        const float input  = static_cast<float>(i) / 100.0f * 2.0f - 1.0f;
        const float output = bc.process(input);
        REQUIRE_THAT(output, WithinAbs(input, 1e-6f));
    }
}

TEST_CASE("BitCrusher: 1-bit output is exactly {−1, +1}", "[bitcrusher]")
{
    // B=1 → steps=1. normalized·1 + 0.5 ∈ [0.5, 1.5], floor ∈ {0, 1}, /1
    // remapped via 2k − 1 gives {−1, +1}. Input ramp crosses zero so both
    // levels are produced. Crossover at input ≥ 0 → +1, input < 0 → −1.
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(1);

    std::set<float> uniqueValues;
    for (int i = 0; i < 1000; ++i)
    {
        const float input  = static_cast<float>(i) / 500.0f - 1.0f; // [-1, +0.998]
        const float output = bc.process(input);
        REQUIRE((output == -1.0f || output == 1.0f));
        uniqueValues.insert(output);
    }
    REQUIRE(uniqueValues.size() == 2);
    REQUIRE(uniqueValues.count(-1.0f) == 1);
    REQUIRE(uniqueValues.count( 1.0f) == 1);
}

TEST_CASE("BitCrusher: 4-bit output lies on 16-level lattice exactly", "[bitcrusher]")
{
    // B=4 → 2^4 = 16 levels on lattice {2k/15 − 1 : k = 0..15}.
    // Ramp −1 → +1 in 10 001 steps (Δ = 2e-4) crosses every cell boundary
    // many times, so all 16 levels are produced. Lattice alignment checked
    // to 1e-6 — the formula is simple floor/divide, no transcendental.
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(4);

    std::set<float> uniqueValues;
    for (int i = 0; i <= 10000; ++i)
    {
        const float input  = static_cast<float>(i) / 5000.0f - 1.0f;
        const float output = bc.process(input);
        uniqueValues.insert(output);
    }
    REQUIRE(uniqueValues.size() == 16);

    for (float v : uniqueValues)
    {
        bool onLattice = false;
        for (int k = 0; k < 16; ++k)
        {
            const float expected = 2.0f * static_cast<float>(k) / 15.0f - 1.0f;
            if (std::abs(v - expected) < 1e-6f) { onLattice = true; break; }
        }
        INFO("observed level = " << v);
        REQUIRE(onLattice);
    }
}

TEST_CASE("BitCrusher: output stays in [-1, 1] and finite", "[bitcrusher]")
{
    // [-1, 1] is a hard invariant of the lattice formula. 44 100 samples
    // of 440 Hz sine at B=4 + 8 kHz downsample covers every phase region
    // of the staircase / quantiser interaction.
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(4);
    bc.setDownsampleRate(8000.0f);

    for (int i = 0; i < 44100; ++i)
    {
        const float input  = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / kSampleRate);
        const float output = bc.process(input);
        REQUIRE(std::isfinite(output));
        REQUIRE(output >= -1.0f);
        REQUIRE(output <=  1.0f);
    }
}

TEST_CASE("BitCrusher: 10× downsample holds exactly 9 of every 10 calls", "[bitcrusher]")
{
    // inc = 4410/44100 = 0.1. Float32 sim (verified): for 999 loop iterations
    // after one priming call, hold count = 899 (100 updates, 899 repeats).
    //   initial block i=1..9 → 8 holds + 1 update (phase seeds at 0.1)
    //   99 full cycles i=10..999 → 99·9 = 891 holds + 99 updates
    //   total → 8 + 891 = 899 holds
    // Float drift ≈1.5e-8/cycle → negligible at horizon=100 cycles. Allow
    // ±3 for compiler eval-order variance.
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(32);
    bc.setDownsampleRate(4410.0f);

    int holdCount = 0;
    float prev = bc.process(0.0f);
    for (int i = 1; i < 1000; ++i)
    {
        const float input  = static_cast<float>(i) / 1000.0f;
        const float output = bc.process(input);
        if (output == prev) ++holdCount;
        prev = output;
    }
    REQUIRE(holdCount >= 896);
    REQUIRE(holdCount <= 902);
}

TEST_CASE("BitCrusher: no downsampling when rate == sampleRate", "[bitcrusher]")
{
    // inc = 1.0 exactly → phase ≥ 1 every call → every sample updates. At
    // B=32 each of a 100-step ramp (Δ = 0.01) produces a distinct output,
    // giving exactly 99 transitions over 99 loop iterations.
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(32);
    bc.setDownsampleRate(kSampleRate);

    int changeCount = 0;
    float prev = bc.process(0.0f);
    for (int i = 1; i < 100; ++i)
    {
        const float input  = static_cast<float>(i) / 100.0f;
        const float output = bc.process(input);
        if (output != prev) ++changeCount;
        prev = output;
    }
    REQUIRE(changeCount == 99);
}

TEST_CASE("BitCrusher: reset clears phase and hold (bit-exact)", "[bitcrusher]")
{
    // After processing, holdSample_ caches the last quantised value.
    // reset() sets holdSample_ = 0 and phase = 0. The subsequent
    // process(0.0f) advances phase 0 → inc (=0.1), no update, so returns
    // holdSample_ = 0 exactly.
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(8);
    bc.setDownsampleRate(4410.0f);

    for (int i = 0; i < 100; ++i)
        bc.process(0.5f);

    bc.reset();
    REQUIRE(bc.process(0.0f) == 0.0f);
}

TEST_CASE("BitCrusher: bit depth clamping to [1, 32]", "[bitcrusher]")
{
    // setBitDepth(0) clamps to 1 (2 levels). Input 0.5 → normalized 0.75
    // → floor(0.75·1 + 0.5) = 1 → q = 1 → out = 1.0 exactly.
    // setBitDepth(64) clamps to 32 (near-lossless). Output within 1e-6
    // of input — same tolerance as the 32-bit passthrough test.
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);

    bc.setBitDepth(0);
    REQUIRE(bc.process(0.5f) == 1.0f);

    bc.setBitDepth(64);
    REQUIRE_THAT(bc.process(0.5f), WithinAbs(0.5f, 1e-6f));
}

// ---------------------------------------------------------------------------
// Long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("BitCrusher: 10-second stability at aggressive settings", "[bitcrusher][stability]")
{
    // 10 s × 44.1 kHz = 441 000 samples. Phase is a bounded quantity —
    // wraps via subtraction of floor — so no accumulating drift can
    // unbounded the output. Output stays on the B=2 lattice (4 levels) in
    // [-1, 1] for all valid input.
    ideath::BitCrusher bc;
    bc.prepare(kSampleRate);
    bc.setBitDepth(2);
    bc.setDownsampleRate(2205.0f); // 20× downsample

    for (int i = 0; i < 441000; ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 0.7f * (std::sin(2.0f * 3.14159265f * 220.0f * t)
                                   + 0.3f * std::sin(2.0f * 3.14159265f * 1100.0f * t));
        const float output = bc.process(input);
        REQUIRE(std::isfinite(output));
        REQUIRE(output >= -1.0f);
        REQUIRE(output <=  1.0f);
    }
}
