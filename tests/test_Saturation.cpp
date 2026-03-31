#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Saturation.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("Saturation::tanhDrive: zero in → zero out", "[sat]")
{
    REQUIRE_THAT(ideath::Saturation::tanhDrive(0.0f, 1.0f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(ideath::Saturation::tanhDrive(0.0f, 5.0f), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Saturation::tanhDrive: drive=1 is near-linear for small input", "[sat]")
{
    float out = ideath::Saturation::tanhDrive(0.1f, 1.0f);
    // tanh(0.1) ≈ 0.0997 — very close to input.
    REQUIRE_THAT(out, WithinAbs(0.1f, 0.01f));
}

TEST_CASE("Saturation::tanhDrive: output is bounded to [-1, 1]", "[sat]")
{
    REQUIRE(ideath::Saturation::tanhDrive(100.0f, 10.0f) <= 1.0f);
    REQUIRE(ideath::Saturation::tanhDrive(-100.0f, 10.0f) >= -1.0f);
}

TEST_CASE("Saturation::tanhDrive: higher drive compresses more", "[sat]")
{
    // With more drive, the *incremental* gain decreases (saturation).
    // Compare how much output changes for the same input delta at different drives.
    float base = 0.3f;
    float bump = 0.6f;

    float deltaLow = ideath::Saturation::tanhDrive(bump, 2.0f) - ideath::Saturation::tanhDrive(base, 2.0f);
    float deltaHigh = ideath::Saturation::tanhDrive(bump, 8.0f) - ideath::Saturation::tanhDrive(base, 8.0f);

    // Higher drive → less incremental output change (more compression).
    REQUIRE(deltaHigh < deltaLow);
}

TEST_CASE("Saturation::tanhDrive: preserves sign", "[sat]")
{
    REQUIRE(ideath::Saturation::tanhDrive(0.5f, 3.0f) > 0.0f);
    REQUIRE(ideath::Saturation::tanhDrive(-0.5f, 3.0f) < 0.0f);
}

TEST_CASE("Saturation::softClip: zero in → zero out", "[sat]")
{
    REQUIRE_THAT(ideath::Saturation::softClip(0.0f), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Saturation::softClip: bounded output", "[sat]")
{
    for (float x = -5.0f; x <= 5.0f; x += 0.1f)
    {
        float y = ideath::Saturation::softClip(x);
        REQUIRE(y >= -1.0f);
        REQUIRE(y <= 1.0f);
    }
}

TEST_CASE("Saturation::softClip: near-linear for small input", "[sat]")
{
    float out = ideath::Saturation::softClip(0.1f);
    REQUIRE_THAT(out, WithinAbs(0.1f, 0.02f));
}

TEST_CASE("Saturation::softClip: preserves sign", "[sat]")
{
    REQUIRE(ideath::Saturation::softClip(0.8f) > 0.0f);
    REQUIRE(ideath::Saturation::softClip(-0.8f) < 0.0f);
}
