#include <ideath/HallReverb.h>
#include <algorithm>
#include <cmath>

namespace ideath {

// Same Freeverb tunings as base Reverb
static constexpr int kCombTunings[8] = {
    1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617
};

static constexpr int kAllpassTunings[4] = {
    556, 441, 341, 225
};

// Per-comb LFO rates (Hz) — spread to avoid correlation
static constexpr float kLfoRates[8] = {
    0.8f, 1.0f, 1.1f, 1.3f, 1.5f, 1.6f, 0.9f, 1.2f
};

// Per-comb LFO phase offsets — evenly spread
static constexpr float kLfoPhases[8] = {
    0.0f, 0.13f, 0.25f, 0.38f, 0.5f, 0.63f, 0.75f, 0.88f
};

static constexpr float kTwoPi = 6.283185307f;

static int scaleTuning(int baseTuning, float sampleRate)
{
    return static_cast<int>(baseTuning * sampleRate / 44100.0f);
}

// --- PreDelay ---

void HallReverb::PreDelay::prepare(int maxSamples)
{
    bufSize = maxSamples + 1;
    buffer.resize(bufSize, 0.0f);
    writeIndex = 0;
    delaySamples = 0;
}

void HallReverb::PreDelay::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}

void HallReverb::PreDelay::setDelay(int samples)
{
    delaySamples = std::min(samples, bufSize - 1);
}

float HallReverb::PreDelay::process(float input)
{
    buffer[writeIndex] = input;

    int readIndex = writeIndex - delaySamples;
    if (readIndex < 0)
        readIndex += bufSize;

    float output = buffer[readIndex];

    if (++writeIndex >= bufSize)
        writeIndex = 0;

    return output;
}

// --- ModCombFilter ---

void HallReverb::ModCombFilter::setSize(int size)
{
    // Extra headroom for modulation excursion
    bufSize = size + 16;
    buffer.resize(bufSize, 0.0f);
    writeIndex = 0;
}

void HallReverb::ModCombFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    filterStore = 0.0f;
    writeIndex = 0;
    lfoPhase = 0.0f;
}

void HallReverb::ModCombFilter::setLfoRate(float rate, float sampleRate)
{
    lfoRate = rate;
    lfoInc = rate / sampleRate;
}

float HallReverb::ModCombFilter::process(float input, float feedback,
                                          float damp1, float damp2,
                                          float modDepth)
{
    // LFO modulates the read position
    float lfo = std::sin(kTwoPi * lfoPhase) * modDepth;
    lfoPhase += lfoInc;
    if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

    // Fractional read position
    int baseDelay = bufSize - 16; // nominal delay length
    float readPos = static_cast<float>(writeIndex) - static_cast<float>(baseDelay) + lfo;
    while (readPos < 0.0f) readPos += static_cast<float>(bufSize);

    // Linear interpolation
    int readIdx0 = static_cast<int>(readPos);
    float frac = readPos - static_cast<float>(readIdx0);
    readIdx0 = readIdx0 % bufSize;
    int readIdx1 = (readIdx0 + 1) % bufSize;

    float output = buffer[readIdx0] * (1.0f - frac) + buffer[readIdx1] * frac;

    // One-pole LP in feedback path
    filterStore = output * damp2 + filterStore * damp1;

    buffer[writeIndex] = input + filterStore * feedback;

    if (++writeIndex >= bufSize)
        writeIndex = 0;

    return output;
}

// --- AllpassFilter ---

void HallReverb::AllpassFilter::setSize(int size)
{
    bufSize = size;
    buffer.resize(size, 0.0f);
    index = 0;
}

void HallReverb::AllpassFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    index = 0;
}

float HallReverb::AllpassFilter::process(float input)
{
    float bufOut = buffer[index];
    float output = bufOut - input;
    buffer[index] = input + bufOut * kFeedback;

    if (++index >= bufSize)
        index = 0;

    return output;
}

// --- HallReverb ---

HallReverb::HallReverb()
{
    prepare(44100.0f);
}

void HallReverb::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;

    int maxPreDelaySamples = static_cast<int>(kMaxPreDelay * sampleRate);
    preDelay_.prepare(maxPreDelaySamples);

    for (int i = 0; i < kNumCombs; ++i)
    {
        int len = scaleTuning(kCombTunings[i], sampleRate);
        combL_[i].setSize(len);
        combR_[i].setSize(len + kStereoSpread);

        combL_[i].setLfoRate(kLfoRates[i], sampleRate);
        combR_[i].setLfoRate(kLfoRates[i] * 1.07f, sampleRate); // slight R detune

        combL_[i].lfoPhase = kLfoPhases[i];
        combR_[i].lfoPhase = kLfoPhases[i] + 0.5f; // opposite phase for stereo
        if (combR_[i].lfoPhase >= 1.0f) combR_[i].lfoPhase -= 1.0f;
    }

    for (int i = 0; i < kNumAllpasses; ++i)
    {
        int len = scaleTuning(kAllpassTunings[i], sampleRate);
        allpassL_[i].setSize(len);
        allpassR_[i].setSize(len + kStereoSpread);
    }

    updateParams();
    reset();
}

void HallReverb::reset()
{
    preDelay_.clear();
    for (auto& c : combL_) c.clear();
    for (auto& c : combR_) c.clear();
    for (auto& a : allpassL_) a.clear();
    for (auto& a : allpassR_) a.clear();

    // Restore LFO phases after clear
    for (int i = 0; i < kNumCombs; ++i)
    {
        combL_[i].lfoPhase = kLfoPhases[i];
        combR_[i].lfoPhase = kLfoPhases[i] + 0.5f;
        if (combR_[i].lfoPhase >= 1.0f) combR_[i].lfoPhase -= 1.0f;
    }
}

void HallReverb::setSize(float size)
{
    size_ = std::clamp(size, 0.0f, 1.0f);
    updateParams();
}

void HallReverb::setDamp(float damp)
{
    damp_ = std::clamp(damp, 0.0f, 1.0f);
    updateParams();
}

void HallReverb::setPreDelay(float seconds)
{
    seconds = std::clamp(seconds, 0.0f, kMaxPreDelay);
    preDelay_.setDelay(static_cast<int>(seconds * sampleRate_));
}

void HallReverb::setModDepth(float depth)
{
    modDepth_ = std::clamp(depth, 0.0f, 1.0f);
}

void HallReverb::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void HallReverb::setFreeze(bool freeze)
{
    freeze_ = freeze;
    updateParams();
}

void HallReverb::updateParams()
{
    if (freeze_)
    {
        feedback_ = 1.0f;
        damp1_ = 0.0f;
        damp2_ = 1.0f;
    }
    else
    {
        // Hall: wider feedback range (0.50–0.99)
        feedback_ = 0.5f + size_ * 0.49f;
        // Hall: low damping for bright, open character (0–0.12)
        damp1_ = damp_ * 0.12f;
        damp2_ = 1.0f - damp1_;
    }
}

std::pair<float, float> HallReverb::process(float input)
{
    // Pre-delay
    float delayed = preDelay_.process(input);

    float in = delayed * kInputGain;

    // Modulation depth in samples (0–7)
    float modSamples = modDepth_ * 7.0f;

    // Parallel modulated comb filters
    float outL = 0.0f;
    float outR = 0.0f;

    for (int i = 0; i < kNumCombs; ++i)
    {
        outL += combL_[i].process(in, feedback_, damp1_, damp2_, modSamples);
        outR += combR_[i].process(in, feedback_, damp1_, damp2_, modSamples);
    }

    // Series allpass filters
    for (int i = 0; i < kNumAllpasses; ++i)
    {
        outL = allpassL_[i].process(outL);
        outR = allpassR_[i].process(outR);
    }

    // Dry/wet mix
    float dry = 1.0f - mix_;
    float wet = mix_;

    float left  = input * dry + outL * wet;
    float right = input * dry + outR * wet;

    return { left, right };
}

} // namespace ideath
