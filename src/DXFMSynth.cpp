#include <ideath/DXFMSynth.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ideath {

namespace {

// FM operator wiring flag bits (matches dexed/msfa convention).
//   bits 0-1: output bus (0=main output, 1=bus 1, 2=bus 2)
//   bit 2:    OUT_BUS_ADD — operator ADDS to its destination bus
//             (carrier flag when destination is the main output)
//   bits 4-5: input bus (0=none, 1=bus 1, 2=bus 2)
//   bit 6:    FB_IN  — operator reads from feedback buffer
//   bit 7:    FB_OUT — operator writes to feedback buffer
//             (an op with both FB_IN and FB_OUT self-feedbacks)
constexpr uint8_t OUT_BUS_ONE  = 1u << 0;
constexpr uint8_t OUT_BUS_TWO  = 1u << 1;
constexpr uint8_t OUT_BUS_ADD  = 1u << 2;
constexpr uint8_t IN_BUS_ONE   = 1u << 4;
constexpr uint8_t IN_BUS_TWO   = 1u << 5;
constexpr uint8_t FB_IN        = 1u << 6;
constexpr uint8_t FB_OUT       = 1u << 7;

// DX7 algorithm wiring table, sourced verbatim from
// https://github.com/asb2m10/dexed (Apache 2.0).  Each of 32 algorithms is 6
// bytes — one per operator, op[0] corresponding to DX7's OP6 (the feedback-
// capable modulator in most algorithms), op[5] to DX7's OP1 (typically a
// carrier).  Reverse numbering relative to DX7 labels but identical topology.
constexpr uint8_t kAlgorithms[32][6] = {
    { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 }, // 1
    { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 }, // 2
    { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 }, // 3
    { 0xc1, 0x11, 0x94, 0x01, 0x11, 0x14 }, // 4
    { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 }, // 5
    { 0xc1, 0x94, 0x01, 0x14, 0x01, 0x14 }, // 6
    { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 }, // 7
    { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 }, // 8
    { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 }, // 9
    { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 }, // 10
    { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 }, // 11
    { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 }, // 12
    { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 }, // 13
    { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 }, // 14
    { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 }, // 15
    { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 }, // 16
    { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 }, // 17
    { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 }, // 18
    { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 }, // 19
    { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 }, // 20
    { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 }, // 21
    { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 }, // 22
    { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 }, // 23
    { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 }, // 24
    { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 }, // 25
    { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 }, // 26
    { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 }, // 27
    { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 }, // 28
    { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 }, // 29
    { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 }, // 30
    { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 }, // 31
    { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 }, // 32
};

constexpr float kTwoPi = 6.28318530717958647692f;

int carrierCountFor(int algorithm)
{
    int count = 0;
    for (int op = 0; op < 6; ++op)
        if ((kAlgorithms[algorithm][op] & 0x07) == OUT_BUS_ADD) ++count;
    return count;
}

} // namespace

bool DXFMSynth::isCarrier(int algorithm, int op)
{
    if (algorithm < 0 || algorithm >= kNumAlgorithms) return false;
    if (op < 0 || op >= kNumOperators) return false;
    return (kAlgorithms[algorithm][op] & OUT_BUS_ADD) != 0;
}

void DXFMSynth::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    for (auto& op : ops_) op.prepareAndReset(sampleRate);
    lfo_.prepare(sampleRate);
    lfo_.setWaveform(LFO::Waveform::Sine);
    lfo_.setPolarity(LFO::Polarity::Bipolar);
    lfo_.setRate(5.0f);
    carrierCount_ = carrierCountFor(algorithm_);
    reset();
}

void DXFMSynth::reset()
{
    for (auto& op : ops_)
    {
        op.phase = 0.0f;
        op.prevOut = 0.0f;
        op.env.reset();
    }
    lfo_.reset();
    feedbackTail_ = 0.0f;
    feedbackPrev_ = 0.0f;
}

void DXFMSynth::noteOn(float freqHz, float velocity)
{
    baseFreq_ = std::max(0.0f, freqHz);
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    updatePhaseIncrements();
    for (auto& op : ops_) op.env.noteOn();
    // LFO phase NOT reset on noteOn — keeps free-running modulation continuous
    // across retriggers (DX7 behaviour for LFO sync = off; full DX7 has a
    // per-patch LFO key-sync flag, omitted here for simplicity).
}

void DXFMSynth::noteOff()
{
    for (auto& op : ops_) op.env.noteOff();
}

bool DXFMSynth::isActive() const
{
    for (const auto& op : ops_)
        if (op.env.isActive()) return true;
    return false;
}

void DXFMSynth::setAlgorithm(int algo)
{
    if (algo < 0 || algo >= kNumAlgorithms) return;
    algorithm_ = algo;
    carrierCount_ = carrierCountFor(algo);
}

void DXFMSynth::setRatio(int op, float ratio)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].ratio = std::clamp(ratio, 0.0f, 32.0f);
    updatePhaseIncrements();
}

void DXFMSynth::setDetune(int op, float cents)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].detuneCents = std::clamp(cents, -100.0f, 100.0f);
    updatePhaseIncrements();
}

void DXFMSynth::setLevel(int op, float level)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].level = std::clamp(level, 0.0f, 1.0f);
}

void DXFMSynth::setFeedback(int op, float amount)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].feedback = std::clamp(amount, 0.0f, 1.0f);
}

void DXFMSynth::setAttack(int op, float seconds)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].env.setAttack(seconds);
}

void DXFMSynth::setDecay(int op, float seconds)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].env.setDecay(seconds);
}

void DXFMSynth::setSustain(int op, float level)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].env.setSustain(level);
}

void DXFMSynth::setRelease(int op, float seconds)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].env.setRelease(seconds);
}

void DXFMSynth::setVelocitySensitivity(int op, float sens)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].velSens = std::clamp(sens, 0.0f, 1.0f);
}

void DXFMSynth::setPMSensitivity(int op, float sens)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].pmSens = std::clamp(sens, 0.0f, 1.0f);
}

void DXFMSynth::setAMSensitivity(int op, float sens)
{
    if (op < 0 || op >= kNumOperators) return;
    ops_[op].amSens = std::clamp(sens, 0.0f, 1.0f);
}

void DXFMSynth::setLFOShape(LFOShape s)
{
    LFO::Waveform w = LFO::Waveform::Sine;
    switch (s)
    {
        case LFOShape::Sine:         w = LFO::Waveform::Sine; break;
        case LFOShape::Triangle:     w = LFO::Waveform::Triangle; break;
        case LFOShape::Square:       w = LFO::Waveform::Square; break;
        case LFOShape::Saw:          w = LFO::Waveform::Saw; break;
        case LFOShape::SampleHold:   w = LFO::Waveform::SampleAndHold; break;
    }
    lfo_.setWaveform(w);
}

void DXFMSynth::setLFORate(float hz)
{
    lfo_.setRate(hz);
}

void DXFMSynth::setLFODepth(float depth)
{
    lfoDepth_ = std::clamp(depth, 0.0f, 1.0f);
}

void DXFMSynth::updatePhaseIncrements()
{
    for (auto& op : ops_)
    {
        const float detuneFactor = (op.detuneCents != 0.0f)
            ? std::pow(2.0f, op.detuneCents / 1200.0f)
            : 1.0f;
        const float freq = baseFreq_ * op.ratio * detuneFactor;
        op.phaseInc = freq / sampleRate_;
    }
}

float DXFMSynth::process()
{
    // Skip the whole render if every operator's envelope is idle.  Saves
    // ~30 ns/sample when the voice is silent.
    if (!isActive()) return 0.0f;

    // LFO sample — bipolar [-1, 1].  When depth=0 the LFO output is multiplied
    // by 0 and the modulation paths are bypassed; we still advance lfo_ to
    // keep its phase consistent across enable/disable transitions.
    const float lfoVal = lfo_.process();
    const float lfoMod = lfoVal * lfoDepth_;

    // 2-sample averaged feedback — same anti-aliasing trick dexed uses.
    // Suppresses the 1-sample-delay alias that bare self-modulation produces.
    const float fbAvg = (feedbackTail_ + feedbackPrev_) * 0.5f;

    // Three accumulators: main output bus and two internal modulation buses.
    // Bus tracking mirrors dexed's `has_contents` array — if an upstream op
    // hasn't written to a bus yet, downstream reads see 0 rather than stale.
    float bus[3] = { 0.0f, 0.0f, 0.0f };
    bool busActive[3] = { false, false, false };
    float newFeedback = 0.0f;

    for (int i = 0; i < kNumOperators; ++i)
    {
        Operator& op = ops_[i];
        const uint8_t flags = kAlgorithms[algorithm_][i];
        const int inbus = (flags >> 4) & 0x3;   // 0=none, 1=bus1, 2=bus2
        const int outbus = flags & 0x3;          // 0=main, 1=bus1, 2=bus2
        const bool addOut = (flags & OUT_BUS_ADD) != 0;

        // --- Modulator input ---
        float modIn = 0.0f;
        if (flags & FB_IN)
        {
            // self-feedback: read averaged tail × per-op feedback gain.
            // ×kTwoPi because the operator's sin() argument is in radians.
            modIn = fbAvg * op.feedback * kTwoPi;
        }
        else if (inbus == 1 && busActive[1])
        {
            modIn = bus[1] * kTwoPi;
        }
        else if (inbus == 2 && busActive[2])
        {
            modIn = bus[2] * kTwoPi;
        }

        // --- LFO pitch modulation (PM) ---
        // Apply as a one-sample phase offset scaled by pmSens × max range
        // (=±0.5 radians at lfoMod=1, pmSens=1 — roughly ±5% pitch wobble).
        // Documented as "±100 cents at depth=1, pmSens=1" in the header;
        // the implementation uses a sine approximation, not literal cents,
        // so the perceived range is similar but not exact.
        const float pmOffset = lfoMod * op.pmSens * 0.5f;

        // --- Phase advance + sine generation ---
        op.phase += op.phaseInc;
        op.phase -= std::floor(op.phase);

        // --- Envelope + level + velocity sens + AM ---
        const float envOut = op.env.process();
        // velSens=0 → factor=1 (ignore velocity); velSens=1 → factor=velocity
        const float velFactor = 1.0f + op.velSens * (velocity_ - 1.0f);
        // AM: 1 + lfoMod × amSens scales between [1-x, 1+x]
        const float amFactor = 1.0f + lfoMod * op.amSens;

        const float opOut = std::sin(op.phase * kTwoPi + modIn + pmOffset)
                          * envOut * op.level * velFactor * amFactor;

        // Only operators with BOTH FB_IN and FB_OUT (= self-feedback) actually
        // write to the feedback buffer.  FB_OUT alone (bytes like 0x94 in
        // algorithms 4/6 of the dexed table) is decorative in the source data
        // but doesn't drive behaviour — dexed's render dispatches `compute_fb`
        // only when `(flags & 0xc0) == 0xc0`, and `compute_pure` (the path
        // taken for FB_OUT-only bytes) never touches the feedback buffer.
        if ((flags & (FB_IN | FB_OUT)) == (FB_IN | FB_OUT))
            newFeedback = opOut;

        op.prevOut = opOut;

        // --- Write to destination bus ---
        if (addOut)
        {
            // Carrier (outbus=0) or accumulating modulator (outbus=1/2)
            if (busActive[outbus])
                bus[outbus] += opOut;
            else
            {
                bus[outbus] = opOut;
                busActive[outbus] = true;
            }
        }
        else
        {
            // Overwriting modulator — replace bus contents unconditionally
            bus[outbus] = opOut;
            busActive[outbus] = true;
        }
    }

    // Shift feedback history.  newFeedback comes from the op that has FB_OUT
    // set (typically op[0] but algorithm-dependent; e.g. algorithm 4's op[2]).
    feedbackPrev_ = feedbackTail_;
    feedbackTail_ = newFeedback;

    // Normalise by carrier count so the output level is comparable across
    // algorithms (alg 1 = 2 carriers vs alg 32 = 6 carriers).  ×0.85 keeps
    // a small headroom for transient overshoot.  Mirrors inboil melodic.ts
    // L211: `out / carrierCount * vel * 0.85`.
    const float scale = (carrierCount_ > 0)
        ? (0.85f / static_cast<float>(carrierCount_))
        : 0.85f;
    return bus[0] * scale;
}

} // namespace ideath
