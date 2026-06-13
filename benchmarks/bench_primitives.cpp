#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ideath/Biquad.h>
#include <ideath/BitCrusher.h>
#include <ideath/Compressor.h>
#include <ideath/CombFilter.h>
#include <ideath/DelayLine.h>
#include <ideath/Envelope.h>
#include <ideath/FMSynth.h>
#include <ideath/FeedbackBuffer.h>
#include <ideath/FunctionGenerator.h>
#include <ideath/GranularProcessor.h>
#include <ideath/HallReverb.h>
#include <ideath/HarmonicOscillator.h>
#include <ideath/BowedString.h>
#include <ideath/LowPassGate.h>
#include <ideath/LowPassGateVoice.h>
#include <ideath/KarplusStrong.h>
#include <ideath/LFO.h>
#include <ideath/ModalResonator.h>
#include <ideath/Noise.h>
#include <ideath/Oscillator.h>
#include <ideath/PeakLimiter.h>
#include <ideath/Portamento.h>
#include <ideath/Reverb.h>
#include <ideath/SVFilter.h>
#include <ideath/Saturation.h>
#include <ideath/ShimmerReverb.h>
#include <ideath/TapeDelay.h>
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

TEST_CASE("Bench: FunctionGenerator", "[bench]")
{
    // Linear curve: hot path is one float add + branch, mirrors a typical
    // slow-modulator workload.
    ideath::FunctionGenerator linear;
    linear.prepare(kSR);
    linear.setRise(0.2f);
    linear.setFall(0.3f);
    linear.setCurve(0.0f);
    linear.setCycle(true);

    BENCHMARK("FunctionGenerator::process (linear, cycle)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += linear.process();
        return acc;
    };

    // Curved variant exercises the pow() shaper on every sample — the
    // realistic worst case for CPU cost.
    ideath::FunctionGenerator curved;
    curved.prepare(kSR);
    curved.setRise(0.2f);
    curved.setFall(0.3f);
    curved.setCurve(0.7f);
    curved.setCycle(true);

    BENCHMARK("FunctionGenerator::process (curve=0.7, cycle)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += curved.process();
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

TEST_CASE("Bench: CombFilter", "[bench]")
{
    ideath::CombFilter comb;
    comb.prepare(kSR, 0.05f);
    comb.setDelay(0.01f);
    comb.setFeedback(0.9f);
    comb.setDamp(0.3f);
    comb.setMix(1.0f);

    BENCHMARK("CombFilter::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += comb.process(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: TapeDelay", "[bench]")
{
    ideath::TapeDelay delay;
    delay.prepare(kSR, 2.0f);
    delay.setDelay(0.3f);
    delay.setFeedback(0.7f);
    delay.setMix(0.5f);
    delay.setWowDepth(0.003f);
    delay.setWowRate(0.4f);
    delay.setFlutterDepth(0.0008f);
    delay.setFlutterRate(4.0f);
    delay.setLowpass(6000.0f);
    delay.setHighpass(80.0f);
    delay.setDrive(2.0f);

    BENCHMARK("TapeDelay::process")
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

TEST_CASE("Bench: Voice (LP filter, Q=4)", "[bench]")
{
    ideath::Voice voice;
    voice.prepare(kSR);
    voice.setSource(ideath::Voice::Source::Oscillator);
    voice.setAttack(0.005f);
    voice.setDecay(0.1f);
    voice.setSustain(0.5f);
    voice.setRelease(0.2f);
    voice.setFilter(ideath::Voice::FilterType::Lowpass, 1000.0f, 4.0f);
    voice.noteOn(440.0f);

    BENCHMARK("Voice::process (LP filter)")
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

TEST_CASE("Bench: KarplusStrong", "[bench]")
{
    // Re-pluck every 2× block to keep the loop alive across the benchmark
    // window without dominating the hot path. The dominant cost is the
    // delay-line readDelay (linear interp) + one-pole LP + the loop-gain
    // multiply; the burst path adds one extra Noise::process call for a
    // few samples per pluck.
    ideath::KarplusStrong ks;
    ks.prepare(kSR);
    ks.setFrequency(220.0f);
    ks.setDecay(1.0f);
    ks.setDamping(0.3f);
    ks.setExciter(1.0f);
    ks.pluck();

    BENCHMARK("KarplusStrong::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
        {
            if ((i % (kBlock / 2)) == 0) ks.pluck();
            acc += ks.process();
        }
        return acc;
    };
}

TEST_CASE("Bench: ModalResonator", "[bench]")
{
    // 8-partial bell, harmonic ratios, modest decay. Strike once before
    // the BENCHMARK block so every measured iteration runs the steady
    // partial-ring hot path (which is the realistic workload for a held
    // bell tail). At default 8 partials the inner loop runs 8 BPs per
    // sample (each multiplied by its cached Q).
    ideath::ModalResonator modal8;
    modal8.prepare(kSR);
    modal8.setPartialCount(8);
    modal8.setFundamental(220.0f);
    for (int i = 0; i < 8; ++i)
        modal8.setPartialDecay(i, 1.0f);
    modal8.strike(1.0f);

    BENCHMARK("ModalResonator::process (8 partials)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += modal8.process();
        return acc;
    };

    // 16-partial worst-case: every mode alive, max parallelism.
    ideath::ModalResonator modal16;
    modal16.prepare(kSR);
    modal16.setPartialCount(ideath::ModalResonator::kMaxPartials);
    modal16.setFundamental(110.0f);  // low fund keeps all 16 partials under Nyquist
    for (int i = 0; i < ideath::ModalResonator::kMaxPartials; ++i)
        modal16.setPartialDecay(i, 1.0f);
    modal16.strike(1.0f);

    BENCHMARK("ModalResonator::process (16 partials)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += modal16.process();
        return acc;
    };
}

TEST_CASE("Bench: GranularProcessor", "[bench]")
{
    // Representative config: grainRate × grainSize = 200 · 0.04 = 8.0
    // expected overlap → keeps ~8 grains in flight at steady state, which
    // is the realistic worst case before the pool of 16 saturates. The
    // hot path then runs an 8-grain inner loop: a linear-interp buffer
    // read + a cos() Hann + a couple of float adds per grain, all behind
    // a deterministic xorshift32 RNG that only fires on spawn boundaries.
    ideath::GranularProcessor gp;
    gp.prepare(kSR, static_cast<int>(kSR));   // 1 s ring buffer
    gp.setGrainRate(200.0f);
    gp.setGrainSize(0.04f);
    gp.setPitchSpread(5.0f);
    gp.setPositionScatter(0.5f);

    // Warm: fill buffer and let ~8 grains spin up before the timed block.
    for (int i = 0; i < static_cast<int>(kSR); ++i)
    {
        gp.writeSample(sineAt(i));
        (void)gp.process();
    }

    BENCHMARK("GranularProcessor::process")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
        {
            gp.writeSample(sineAt(i));
            acc += gp.process();
        }
        return acc;
    };
}

TEST_CASE("Bench: LowPassGate", "[bench]")
{
    // Realistic Ping config: trigger once, let the envelope settle into
    // Decay (where most of the LPG's runtime lives — the attack phase
    // is sub-millisecond).  Carrier is a 440 Hz sine; the LPG runs an
    // exp + Biquad coefficient recompute + Biquad process + multiply
    // per sample.
    ideath::LowPassGate lpg;
    lpg.prepare(kSR);
    lpg.setBrightness(0.7f);
    lpg.setDamping(1.0f);   // long fall — envelope stays > kSilenceThreshold for the whole bench
    lpg.trigger(1.0f);
    for (int i = 0; i < static_cast<int>(kSR * 0.01f); ++i)
        (void)lpg.process(sineAt(i));

    BENCHMARK("LowPassGate::process (Decay stage)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += lpg.process(sineAt(i));
        return acc;
    };
}

TEST_CASE("Bench: LowPassGateVoice", "[bench]")
{
    // The bundled voice adds a saw↔square Oscillator (≈ 5.5 ns/sample
    // per Oscillator bench row) to the LPG cost.  Realistic Ping use
    // case is pinged-once + steady ring.
    ideath::LowPassGateVoice voice;
    voice.prepare(kSR);
    voice.setFrequency(440.0f);
    voice.setTone(1.0f);
    voice.setBrightness(0.7f);
    voice.setDamping(1.0f);
    voice.ping(1.0f);
    for (int i = 0; i < static_cast<int>(kSR * 0.01f); ++i)
        (void)voice.process();

    BENCHMARK("LowPassGateVoice::process (Decay stage)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += voice.process();
        return acc;
    };
}

TEST_CASE("Bench: BowedString", "[bench]")
{
    // Slothrop Bow engine steady-state config: bow held at the friction
    // peak velocity, moderate pressure and damping, mid-bridge pickup.
    // The inner loop is two DelayLine reads + one std::exp + one std::tanh
    // per sample plus the LP filter — comparable workload to KS plus the
    // friction nonlinearity.
    ideath::BowedString b;
    b.prepare(kSR);
    b.setFrequency(220.0f);
    b.setPressure(0.6f);
    b.setPosition(0.2f);
    b.setDamping(0.3f);
    b.setBowVelocity(0.3f);

    // Warm: 1 s of bowing so the loop is in steady state before timing.
    for (int i = 0; i < static_cast<int>(kSR); ++i)
        (void)b.process();

    BENCHMARK("BowedString::process (steady bow)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += b.process();
        return acc;
    };
}

TEST_CASE("Bench: HarmonicOscillator", "[bench]")
{
    // 8-partial config — representative LOW+MID-band usage where slothrop's
    // Loom engine spends most of its time. Inner loop runs 8 sin() calls
    // per sample after the amp==0 skip filters out HIGH-band partials.
    ideath::HarmonicOscillator h8;
    h8.prepare(kSR);
    h8.setFrequency(220.0f);
    h8.setPartialCount(8);
    h8.setBands(1.0f, 1.0f, 0.0f, 0.0f);

    BENCHMARK("HarmonicOscillator::process (8 partials)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += h8.process();
        return acc;
    };

    // 32-partial worst case: low fundamental keeps every partial below
    // Nyquist guard, all amps non-zero so no skip. This is the bound the
    // iPhone-class CPU budget must absorb for a single Loom voice.
    ideath::HarmonicOscillator h32;
    h32.prepare(kSR);
    h32.setFrequency(55.0f);   // 55 × 32 = 1760 Hz, all 32 alive
    h32.setPartialCount(ideath::HarmonicOscillator::kMaxPartials);
    h32.setBands(1.0f, 1.0f, 1.0f, 0.0f);

    BENCHMARK("HarmonicOscillator::process (32 partials)")
    {
        float acc = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            acc += h32.process();
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
