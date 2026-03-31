#include <catch2/catch_test_macros.hpp>
#include <ideath/Oscillator.h>
#include <ideath/Wavetable.h>
#include <ideath/Noise.h>
#include <ideath/Biquad.h>
#include <ideath/BitCrusher.h>
#include <ideath/Saturation.h>
#include <ideath/DelayLine.h>
#include <ideath/Envelope.h>
#include <ideath/LFO.h>
#include <cmath>
#include <vector>

static constexpr float kSampleRate = 44100.0f;
static constexpr int kOneSecond = 44100;

// Hard ceiling: no sample should ever exceed this
static constexpr float kMaxSafeLevel = 10.0f;

static float peakLevel(const std::vector<float>& buf)
{
    float peak = 0.0f;
    for (float s : buf)
    {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    return peak;
}

static float rms(const std::vector<float>& buf)
{
    double sum = 0.0;
    for (float s : buf)
        sum += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buf.size())));
}

TEST_CASE("Safety: high-Q filter does not explode", "[safety]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    ideath::Biquad filter;

    // Extreme Q value
    filter.setLowpass(1000.0f, 50.0f, kSampleRate);

    std::vector<float> buf(kOneSecond);
    for (int i = 0; i < kOneSecond; ++i)
    {
        float s = osc.process(1.0f); // saw
        buf[static_cast<size_t>(i)] = filter.process(s);
    }

    float peak = peakLevel(buf);
    REQUIRE(peak < kMaxSafeLevel);
    // High Q will amplify, but should not be unbounded
    REQUIRE(std::isfinite(peak));
}

TEST_CASE("Safety: high feedback delay does not diverge", "[safety]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    ideath::DelayLine delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.1f);
    delay.setFeedback(0.99f);
    delay.setMix(0.5f);

    // Feed signal for 0.1s then silence for 2s
    std::vector<float> buf(kOneSecond * 2);
    for (int i = 0; i < kOneSecond * 2; ++i)
    {
        float input = (i < 4410) ? osc.process(1.0f) : 0.0f;
        buf[static_cast<size_t>(i)] = delay.process(input);
    }

    float peak = peakLevel(buf);
    REQUIRE(peak < kMaxSafeLevel);
    REQUIRE(std::isfinite(peak));

    // Tail should decay, not grow — last 0.5s should be quieter than first 0.5s
    std::vector<float> firstHalf(buf.begin(), buf.begin() + kOneSecond);
    std::vector<float> lastHalf(buf.begin() + kOneSecond, buf.end());
    REQUIRE(rms(lastHalf) < rms(firstHalf));
}

TEST_CASE("Safety: feedback 1.0 delay is bounded", "[safety]")
{
    ideath::DelayLine delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.05f);
    delay.setFeedback(1.0f); // edge case: unity feedback
    delay.setMix(0.5f);

    // Single impulse
    delay.process(1.0f);
    for (int i = 1; i < kOneSecond; ++i)
    {
        float s = delay.process(0.0f);
        REQUIRE(std::fabs(s) <= 1.5f); // should not grow beyond impulse level
        REQUIRE(std::isfinite(s));
    }
}

TEST_CASE("Safety: full chain output is bounded", "[safety]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    ideath::Biquad filter;
    filter.setLowpass(1000.0f, 5.0f, kSampleRate);

    ideath::BitCrusher crush;
    crush.prepare(kSampleRate);
    crush.setBitDepth(4);
    crush.setDownsampleRate(8000.0f);

    ideath::DelayLine delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.3f);
    delay.setFeedback(0.5f);
    delay.setMix(0.5f);

    std::vector<float> buf(kOneSecond);
    for (int i = 0; i < kOneSecond; ++i)
    {
        float s = osc.process(0.0f); // square
        s = filter.process(s);
        s = crush.process(s);
        s = ideath::Saturation::tanhDrive(s, 3.0f);
        s = delay.process(s);
        buf[static_cast<size_t>(i)] = s;
    }

    float peak = peakLevel(buf);
    REQUIRE(peak < kMaxSafeLevel);
    REQUIRE(std::isfinite(peak));
}

TEST_CASE("Safety: no NaN or Inf from extreme parameters", "[safety]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);

    ideath::Biquad filter;

    // Extreme frequency values
    float extremeFreqs[] = { 0.0f, 0.001f, 20000.0f, 22050.0f };
    float extremeQs[] = { 0.01f, 0.1f, 100.0f };

    for (float freq : extremeFreqs)
    {
        for (float q : extremeQs)
        {
            filter.setLowpass(freq, q, kSampleRate);
            osc.setFrequency(440.0f);

            for (int i = 0; i < 1000; ++i)
            {
                float s = filter.process(osc.process(1.0f));
                REQUIRE(std::isfinite(s));
            }

            filter.reset();
        }
    }
}

TEST_CASE("Safety: LFO modulated filter stays stable", "[safety]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    ideath::LFO lfo;
    lfo.prepare(kSampleRate);
    lfo.setRate(10.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Sine);
    lfo.setPolarity(ideath::LFO::Polarity::Bipolar);

    ideath::Biquad filter;

    std::vector<float> buf(kOneSecond);
    for (int i = 0; i < kOneSecond; ++i)
    {
        float lfoVal = lfo.process();
        // Sweep filter from 100 Hz to 10000 Hz
        float cutoff = 1000.0f * std::pow(2.0f, lfoVal * 3.3f);
        filter.setLowpass(cutoff, 5.0f, kSampleRate);

        float s = osc.process(1.0f);
        buf[static_cast<size_t>(i)] = filter.process(s);
    }

    float peak = peakLevel(buf);
    REQUIRE(peak < kMaxSafeLevel);
    REQUIRE(std::isfinite(peak));
}

TEST_CASE("Safety: filter does not silence the signal", "[safety]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    ideath::Biquad filter;

    // Lowpass well above signal frequency — signal should pass through
    filter.setLowpass(4000.0f, 0.707f, kSampleRate);

    std::vector<float> buf(kOneSecond);
    for (int i = 0; i < kOneSecond; ++i)
        buf[static_cast<size_t>(i)] = filter.process(osc.process(1.0f));

    REQUIRE(rms(buf) > 0.1f);
}

TEST_CASE("Safety: full chain produces audible output", "[safety]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    ideath::Biquad filter;
    filter.setLowpass(2000.0f, 0.707f, kSampleRate);

    ideath::BitCrusher crush;
    crush.prepare(kSampleRate);
    crush.setBitDepth(8);
    crush.setDownsampleRate(22050.0f);

    ideath::DelayLine delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.3f);
    delay.setFeedback(0.3f);
    delay.setMix(0.5f);

    std::vector<float> buf(kOneSecond);
    for (int i = 0; i < kOneSecond; ++i)
    {
        float s = osc.process(0.0f); // square
        s = filter.process(s);
        s = crush.process(s);
        s = delay.process(s);
        buf[static_cast<size_t>(i)] = s;
    }

    REQUIRE(rms(buf) > 0.05f);
    REQUIRE(peakLevel(buf) > 0.1f);
}

TEST_CASE("Safety: wavetable produces audible output through chain", "[safety]")
{
    auto wt = ideath::Wavetable::squareTable();
    wt.prepare(kSampleRate);
    wt.setFrequency(440.0f);

    ideath::Biquad filter;
    filter.setLowpass(2000.0f, 0.707f, kSampleRate);

    std::vector<float> buf(kOneSecond);
    for (int i = 0; i < kOneSecond; ++i)
        buf[static_cast<size_t>(i)] = filter.process(wt.process());

    REQUIRE(rms(buf) > 0.1f);
}

TEST_CASE("Safety: envelope produces audible sustain", "[safety]")
{
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0f);

    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);
    env.setDecay(0.1f);
    env.setSustain(0.7f);
    env.setRelease(0.3f);
    env.noteOn();

    // Run for 0.5s (well into sustain phase)
    std::vector<float> buf(kOneSecond / 2);
    for (int i = 0; i < kOneSecond / 2; ++i)
        buf[static_cast<size_t>(i)] = osc.process(1.0f) * env.process();

    REQUIRE(rms(buf) > 0.1f);
}

TEST_CASE("Safety: saturation always limits output", "[safety]")
{
    // Even with extreme input, saturation should bound output
    float extremeInputs[] = { -100.0f, -10.0f, -1.0f, 0.0f, 1.0f, 10.0f, 100.0f };
    float drives[] = { 0.1f, 1.0f, 5.0f, 20.0f, 100.0f };

    for (float input : extremeInputs)
    {
        for (float drive : drives)
        {
            float s = ideath::Saturation::tanhDrive(input, drive);
            REQUIRE(std::isfinite(s));
            REQUIRE(std::fabs(s) <= 1.01f); // tanh is bounded to [-1, 1]
        }

        float s = ideath::Saturation::softClip(input);
        REQUIRE(std::isfinite(s));
        REQUIRE(std::fabs(s) <= 1.01f);
    }
}
