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
    REQUIRE(std::isfinite(peak));
    // LP biquad peak gain at resonance = Q. Saw harmonics near fc=1000Hz:
    // 2nd (880Hz) amp ≈ 0.32, gain ≈ 4.4 → 1.41. 1st (440Hz) amp ≈ 0.64,
    // gain ≈ 1.26 → 0.80. Sum of all |H(fn)|×an ≈ 3. Ringing overshoot
    // during transient adds ~50%. Bound = 5.0 (= Q/10, well below instability).
    REQUIRE(peak < 5.0f);
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
    REQUIRE(std::isfinite(peak));
    // feedback=0.99, mix=0.5. During 0.1s burst: output = input×0.5 (wet=0, buffer
    // still filling). After burst: output = wet×0.5, decaying at 0.99/tap.
    // Delay = 0.1s = burst duration, so first recirculation arrives as burst ends.
    // Peak output = 0.5 (dry half of input). Bound 2.0 adds 4× margin.
    REQUIRE(peak < 2.0f);

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
        REQUIRE(std::isfinite(s));
        // setFeedback(1.0) clamped to 0.999. Single impulse, mix=0.5.
        // First output: dry = 1.0×0.5 = 0.5, wet = 0 → 0.5.
        // Each echo: wet×0.5, decaying by 0.999/tap. Peak = 0.5.
        // Delay = 0.05s = 2205 samples (integer), no interpolation error.
        REQUIRE(std::fabs(s) <= 0.51f);
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
    REQUIRE(std::isfinite(peak));
    // Chain: osc(±1) → LP Q=5 → BitCrusher(±1) → tanh(x,3) → |output|<1.
    // After tanh, signal is bounded by ±1. Delay mix=0.5, fb=0.5:
    // steady-state buffer = input/(1−fb) = 2×input. Output = dry×0.5 + wet×0.5
    // = 0.5 + 1.0 = 1.5 at most. Bound 2.0 adds 33% margin.
    REQUIRE(peak < 2.0f);
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
    REQUIRE(std::isfinite(peak));
    // LP Q=5, cutoff sweeping 100–10kHz via LFO. Max gain at resonance = Q = 5.
    // Sweeping prevents sustained resonance build-up. Saw input ±1.
    // Bound = Q = 5.0 (theoretical maximum if harmonic lands exactly on resonance).
    REQUIRE(peak < 5.0f);
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

    // Saw 440Hz through LP 4kHz Butterworth (Q=0.707): harmonics 1–9 (up to
    // 3960Hz) pass nearly unattenuated. These carry most energy. Saw RMS =
    // 1/√3 ≈ 0.577; slight HF rolloff gives ~0.55. Threshold 0.4 is ~27% below.
    REQUIRE(rms(buf) > 0.4f);
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

    // Chain: square(±1) → LP 2kHz Q=0.707 → BitCrusher 8bit/22050 → delay.
    // LP passes fundamental (440Hz) + 3rd harmonic (1320Hz); higher rolled off.
    // BitCrusher 8-bit: 256 levels, negligible quantization loss for these amplitudes.
    // 22050 Hz downsample holds every other sample: ~0.7× energy factor.
    // Delay mix=0.5 fb=0.3: dry×0.5 + wet×0.5, adding energy from recirculation.
    // Conservative estimate: RMS ≈ 0.3. Threshold 0.15 is 50% below.
    REQUIRE(rms(buf) > 0.15f);
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

    // Wavetable square (±1) through LP 2kHz Butterworth: fundamental (440Hz)
    // passes at unity, odd harmonics 3rd (1320Hz) mostly pass, 5th (2200Hz)
    // near cutoff. Square RMS = 1.0; after LP rolloff ≈ 0.7. Threshold 0.3
    // is ~57% below expected.
    REQUIRE(rms(buf) > 0.3f);
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

    // Saw RMS ≈ 1/√3 ≈ 0.577. ADSR: attack=0.01s to peak, decay=0.1s to
    // sustain=0.7, then sustain for remaining ~0.39s. Weighted RMS:
    // ≈ sqrt((0.11×0.5² + 0.39×(0.577×0.7)²) / 0.5) ≈ 0.43.
    // Threshold 0.3 is ~30% below expected.
    REQUIRE(rms(buf) > 0.3f);
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
            // tanh(x) ∈ (−1, 1) for all finite x. IEEE 754 float:
            // std::tanh(FLT_MAX) = 1.0f exactly. Bound is tight.
            REQUIRE(std::fabs(s) <= 1.0f);
        }

        float s = ideath::Saturation::softClip(input);
        REQUIRE(std::isfinite(s));
        // softClip: x − x³/3 on [−1,1], else ±2/3. Max |output| = 2/3
        // at x=±1 and for |x|>1. Bound = 2/3 + float epsilon.
        REQUIRE(std::fabs(s) <= 2.0f / 3.0f + 1e-6f);
    }
}
