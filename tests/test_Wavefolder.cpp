#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Wavefolder.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("Wavefolder: zero in → zero out", "[wavefolder]")
{
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    REQUIRE_THAT(wf.process(0.0f), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Wavefolder: drive=1 is near-linear for small input", "[wavefolder]")
{
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(1.0f);
    // sin(0.1 * 1) ≈ 0.0998
    float out = wf.process(0.1f);
    REQUIRE_THAT(out, WithinAbs(0.1f, 0.01f));
}

TEST_CASE("Wavefolder: output is bounded to [-1, 1]", "[wavefolder]")
{
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(10.0f);

    for (float x = -2.0f; x <= 2.0f; x += 0.01f)
    {
        float y = wf.process(x);
        REQUIRE(y >= -1.0f);
        REQUIRE(y <= 1.0f);
    }
}

TEST_CASE("Wavefolder: higher drive produces folding", "[wavefolder]")
{
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);

    // With drive=1, output is monotonically increasing for input [0, 1]
    // With high drive, the output should fold back (non-monotonic)
    wf.setDrive(8.0f);
    float prev = wf.process(0.0f);
    bool folded = false;
    for (float x = 0.05f; x <= 1.0f; x += 0.05f)
    {
        float curr = wf.process(x);
        if (curr < prev)
            folded = true;
        prev = curr;
    }
    REQUIRE(folded);
}

TEST_CASE("Wavefolder: preserves sign symmetry", "[wavefolder]")
{
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(3.0f);

    float pos = wf.process(0.5f);
    float neg = wf.process(-0.5f);
    REQUIRE_THAT(pos, WithinAbs(-neg, 1e-6f));
}

TEST_CASE("Wavefolder: mix=0 is fully dry", "[wavefolder]")
{
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(5.0f);
    wf.setMix(0.0f);

    float input = 0.7f;
    REQUIRE_THAT(wf.process(input), WithinAbs(input, 1e-6f));
}

TEST_CASE("Wavefolder: mix=0.5 blends dry and wet", "[wavefolder]")
{
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(3.0f);
    wf.setMix(0.5f);

    float input = 0.5f;
    float wet = std::sin(input * 3.0f);
    float expected = 0.5f * input + 0.5f * wet;
    REQUIRE_THAT(wf.process(input), WithinAbs(expected, 1e-6f));
}

TEST_CASE("Wavefolder: reset does not crash", "[wavefolder]")
{
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(5.0f);
    wf.process(0.5f);
    wf.reset();
    // After reset, should still work
    REQUIRE_THAT(wf.process(0.0f), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Wavefolder: drive is clamped to >= 1", "[wavefolder]")
{
    ideath::Wavefolder wf;
    wf.prepare(44100.0f);
    wf.setDrive(0.0f); // should clamp to 1.0
    // sin(0.5 * 1.0) ≈ 0.479
    float out = wf.process(0.5f);
    float expected = std::sin(0.5f * 1.0f);
    REQUIRE_THAT(out, WithinAbs(expected, 1e-5f));
}
