#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/Portamento.h>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

TEST_CASE("Portamento: instant glide when time is 0", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.0f);
    port.setValue(0.0f);
    port.setTarget(1.0f);

    float out = port.process();
    REQUIRE_THAT(out, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("Portamento: reaches target eventually", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.1f); // 100ms
    port.setValue(0.0f);
    port.setTarget(1.0f);

    // Run for 500ms — should be very close to target
    for (int i = 0; i < 22050; ++i)
        port.process();

    REQUIRE_THAT(port.getValue(), WithinAbs(1.0f, 0.001f));
}

TEST_CASE("Portamento: glide is gradual", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.1f);
    port.setValue(0.0f);
    port.setTarget(1.0f);

    // After a few samples, should be between 0 and 1
    for (int i = 0; i < 100; ++i)
        port.process();

    float val = port.getValue();
    REQUIRE(val > 0.01f);
    REQUIRE(val < 0.99f);
}

TEST_CASE("Portamento: glides downward too", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.05f);
    port.setValue(1.0f);
    port.setTarget(0.0f);

    for (int i = 0; i < 100; ++i)
        port.process();

    float val = port.getValue();
    REQUIRE(val < 0.99f);
    REQUIRE(val > 0.01f);

    // Run longer
    for (int i = 0; i < 22050; ++i)
        port.process();

    REQUIRE_THAT(port.getValue(), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("Portamento: setValue jumps immediately", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(1.0f);
    port.setValue(0.5f);

    REQUIRE_THAT(port.getValue(), WithinAbs(0.5f, 0.001f));
}

TEST_CASE("Portamento: changing target mid-glide", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.1f);
    port.setValue(0.0f);
    port.setTarget(1.0f);

    // Glide partway
    for (int i = 0; i < 1000; ++i)
        port.process();

    float midValue = port.getValue();
    REQUIRE(midValue > 0.0f);
    REQUIRE(midValue < 1.0f);

    // Change target
    port.setTarget(-1.0f);

    // Run long enough to reach new target
    for (int i = 0; i < 44100; ++i)
        port.process();

    REQUIRE_THAT(port.getValue(), WithinAbs(-1.0f, 0.001f));
}

TEST_CASE("Portamento: reset clears state", "[portamento]")
{
    ideath::Portamento port;
    port.prepare(kSampleRate);
    port.setTime(0.1f);
    port.setValue(0.8f);
    port.setTarget(0.8f);

    port.reset();

    REQUIRE_THAT(port.getValue(), WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(port.getTarget(), WithinAbs(0.0f, 0.001f));
}
