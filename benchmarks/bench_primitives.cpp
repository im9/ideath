#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ideath/Biquad.h>
#include <ideath/BitCrusher.h>
#include <ideath/Compressor.h>
#include <ideath/DelayLine.h>
#include <ideath/Envelope.h>
#include <ideath/FMSynth.h>
#include <ideath/FeedbackBuffer.h>
#include <ideath/HallReverb.h>
#include <ideath/LFO.h>
#include <ideath/Noise.h>
#include <ideath/Oscillator.h>
#include <ideath/PeakLimiter.h>
#include <ideath/Portamento.h>
#include <ideath/Reverb.h>
#include <ideath/SVFilter.h>
#include <ideath/Saturation.h>
#include <ideath/ShimmerReverb.h>
#include <ideath/UnisonOscillator.h>
#include <ideath/Voice.h>
#include <ideath/Wavefolder.h>
#include <ideath/Wavetable.h>

#include <cmath>

namespace {

constexpr float kSR = 48000.0f;
constexpr int kBlock = 512;
constexpr float kPi = 3.14159265358979323846f;

float sineAt(int i, float freq = 440.0f)
{
    return std::sin(2.0f * kPi * freq * static_cast<float>(i) / kSR);
}

} // namespace

TEST_CASE("Bench: Oscillator", "[bench]")
{
    ideath::Oscillator saw;
    saw.prepare(kSR);
    saw.setFrequency(440.0f);

    BENCHMARK("Oscillator::process (saw)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += saw.process(1.0f);
        return acc;
    };

    ideath::Oscillator square;
    square.prepare(kSR);
    square.setFrequency(440.0f);

    BENCHMARK("Oscillator::process (square)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += square.process(0.0f);
        return acc;
    };
}

TEST_CASE("Bench: SVFilter", "[bench]")
{
    ideath::SVFilter filter;
    filter.prepare(kSR);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.7f);
    filter.setMode(ideath::SVFilter::Mode::Lowpass);

    BENCHMARK("SVFilter::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += filter.process(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: Biquad", "[bench]")
{
    ideath::Biquad filter;
    filter.setLowpass(1000.0f, 1.0f, kSR);

    BENCHMARK("Biquad::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += filter.process(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: ADSR Envelope", "[bench]")
{
    ideath::AdsrEnvelope env;
    env.prepare(kSR);
    env.setAttack(0.005f);
    env.setDecay(0.1f);
    env.setSustain(0.5f);
    env.setRelease(0.2f);
    env.noteOn();

    BENCHMARK("AdsrEnvelope::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += env.process();
        return acc;
    };
}

TEST_CASE("Bench: Decay Envelope", "[bench]")
{
    ideath::DecayEnvelope env;
    env.prepare(kSR);
    env.setDecay(0.1f);
    env.trigger();

    BENCHMARK("DecayEnvelope::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += env.process();
        return acc;
    };
}

TEST_CASE("Bench: Wavetable", "[bench]")
{
    ideath::Wavetable wt = ideath::Wavetable::sawTable();
    wt.prepare(kSR);
    wt.setInterpolation(ideath::Wavetable::Interpolation::Linear);
    wt.setFrequency(440.0f);

    BENCHMARK("Wavetable::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += wt.process();
        return acc;
    };
}

TEST_CASE("Bench: LFO", "[bench]")
{
    ideath::LFO lfo;
    lfo.prepare(kSR);
    lfo.setRate(5.0f);
    lfo.setWaveform(ideath::LFO::Waveform::Sine);

    BENCHMARK("LFO::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += lfo.process();
        return acc;
    };
}

TEST_CASE("Bench: Noise", "[bench]")
{
    ideath::Noise noise;

    BENCHMARK("Noise::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += noise.process();
        return acc;
    };
}

TEST_CASE("Bench: Saturation", "[bench]")
{
    BENCHMARK("Saturation::tanhDrive")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += ideath::Saturation::tanhDrive(sineAt(i), 3.0f);
        return acc;
    };

    BENCHMARK("Saturation::softClip")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += ideath::Saturation::softClip(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: BitCrusher", "[bench]")
{
    ideath::BitCrusher crush;
    crush.prepare(kSR);
    crush.setBitDepth(8);
    crush.setDownsampleRate(kSR / 4.0f);

    BENCHMARK("BitCrusher::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += crush.process(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: DelayLine", "[bench]")
{
    ideath::DelayLine delay;
    delay.prepare(kSR, 1.0f);
    delay.setDelay(0.3f);
    delay.setFeedback(0.5f);
    delay.setMix(0.5f);

    BENCHMARK("DelayLine::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += delay.process(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: Portamento", "[bench]")
{
    ideath::Portamento porta;
    porta.prepare(kSR);
    porta.setTime(0.01f);
    porta.setValue(220.0f);
    porta.setTarget(880.0f);

    BENCHMARK("Portamento::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += porta.process();
        return acc;
    };
}

TEST_CASE("Bench: Wavefolder", "[bench]")
{
    ideath::Wavefolder folder;
    folder.prepare(kSR);
    folder.setDrive(3.0f);
    folder.setMix(1.0f);

    BENCHMARK("Wavefolder::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += folder.process(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: Compressor", "[bench]")
{
    ideath::Compressor comp;
    comp.prepare(kSR);
    comp.setThreshold(-12.0f);
    comp.setRatio(4.0f);
    comp.setAttack(0.005f);
    comp.setRelease(0.05f);
    comp.setMakeup(0.0f);
    comp.setKnee(6.0f);

    BENCHMARK("Compressor::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += comp.process(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: PeakLimiter", "[bench]")
{
    ideath::PeakLimiter limiter;
    limiter.prepare(kSR);
    limiter.setThreshold(-1.0f);
    limiter.setRelease(0.005f);
    limiter.setLookahead(0.005f);

    BENCHMARK("PeakLimiter::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += limiter.process(sineAt(i) * 1.5f);
        return acc;
    };
}

TEST_CASE("Bench: Reverb", "[bench]")
{
    ideath::Reverb reverb;
    reverb.prepare(kSR);
    reverb.setSize(0.8f);
    reverb.setDamp(0.3f);
    reverb.setMix(0.5f);

    BENCHMARK("Reverb::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
        {
            auto [l, r] = reverb.process(sineAt(i));
            acc += l + r;
        }
        return acc;
    };
}

TEST_CASE("Bench: HallReverb", "[bench]")
{
    ideath::HallReverb reverb;
    reverb.prepare(kSR);
    reverb.setSize(0.8f);
    reverb.setDamp(0.3f);
    reverb.setPreDelay(0.02f);
    reverb.setModDepth(0.2f);
    reverb.setMix(0.5f);

    BENCHMARK("HallReverb::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
        {
            auto [l, r] = reverb.process(sineAt(i));
            acc += l + r;
        }
        return acc;
    };
}

TEST_CASE("Bench: ShimmerReverb", "[bench]")
{
    ideath::ShimmerReverb reverb;
    reverb.prepare(kSR);
    reverb.setSize(0.8f);
    reverb.setDamp(0.3f);
    reverb.setShimmer(0.5f);
    reverb.setMix(0.5f);

    BENCHMARK("ShimmerReverb::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
        {
            auto [l, r] = reverb.process(sineAt(i));
            acc += l + r;
        }
        return acc;
    };
}

TEST_CASE("Bench: UnisonOscillator", "[bench]")
{
    ideath::UnisonOscillator osc;
    osc.prepare(kSR);
    osc.setVoiceCount(5);
    osc.setFrequency(440.0f);
    osc.setDetune(20.0f);

    BENCHMARK("UnisonOscillator::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += osc.process(1.0f);
        return acc;
    };
}

TEST_CASE("Bench: FMSynth", "[bench]")
{
    ideath::FMSynth fm;
    fm.prepare(kSR);
    fm.setAlgorithm(0);
    for (int op = 0; op < ideath::FMSynth::kNumOperators; ++op)
    {
        fm.setRatio(op, 1.0f + 0.5f * static_cast<float>(op));
        fm.setLevel(op, op == 0 ? 1.0f : 0.5f);
        fm.setFeedback(op, op == 0 ? 0.2f : 0.0f);
        fm.setAttack(op, 0.005f);
        fm.setDecay(op, 0.1f);
        fm.setSustain(op, 0.5f);
        fm.setRelease(op, 0.2f);
    }
    fm.noteOn(440.0f);

    BENCHMARK("FMSynth::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += fm.process();
        return acc;
    };
}

TEST_CASE("Bench: Voice", "[bench]")
{
    ideath::Voice voice;
    voice.prepare(kSR);
    voice.setSource(ideath::Voice::Source::Oscillator);
    voice.setAttack(0.005f);
    voice.setDecay(0.1f);
    voice.setSustain(0.5f);
    voice.setRelease(0.2f);
    voice.noteOn(440.0f);

    BENCHMARK("Voice::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += voice.process();
        return acc;
    };
}

TEST_CASE("Bench: FeedbackBuffer", "[bench]")
{
    ideath::FeedbackBuffer buffer;
    buffer.prepare(kSR, 30.0f);
    buffer.setFeedback(0.7f);
    buffer.setMix(1.0f);
    buffer.record();
    for (int i = 0; i < kBlock; ++i)
        buffer.process(sineAt(i));
    buffer.stop();
    buffer.play();

    BENCHMARK("FeedbackBuffer::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += buffer.process(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: Reference chain", "[bench]")
{
    ideath::Oscillator osc;
    osc.prepare(kSR);
    osc.setFrequency(440.0f);

    ideath::SVFilter filter;
    filter.prepare(kSR);
    filter.setCutoff(800.0f);
    filter.setResonance(0.6f);
    filter.setMode(ideath::SVFilter::Mode::Lowpass);

    ideath::AdsrEnvelope env;
    env.prepare(kSR);
    env.setAttack(0.005f);
    env.setDecay(0.1f);
    env.setSustain(0.5f);
    env.setRelease(0.2f);
    env.noteOn();

    ideath::Reverb reverb;
    reverb.prepare(kSR);
    reverb.setSize(0.8f);
    reverb.setDamp(0.3f);
    reverb.setMix(0.5f);

    BENCHMARK("Osc -> SVFilter -> ADSR -> Saturation -> Reverb")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
        {
            float sample = osc.process(1.0f);
            sample = filter.process(sample);
            sample *= env.process();
            sample = ideath::Saturation::tanhDrive(sample, 2.0f);
            auto [l, r] = reverb.process(sample);
            acc += l + r;
        }
        return acc;
    };
}
