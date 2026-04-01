#include <ideath/Reverb.h>
#include <algorithm>
#include <cmath>

namespace ideath {

// Freeverb comb filter delay lengths (at 44100 Hz)
static constexpr int kCombTunings[8] = {
    1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617
};

// Freeverb allpass filter delay lengths (at 44100 Hz)
static constexpr int kAllpassTunings[4] = {
    556, 441, 341, 225
};

// Scale delay lengths for different sample rates
static int scaleTuning(int baseTuning, float sampleRate)
{
    return static_cast<int>(baseTuning * sampleRate / 44100.0f);
}

// --- CombFilter ---

void Reverb::CombFilter::setSize(int size)
{
    bufSize = size;
    buffer.resize(size, 0.0f);
    index = 0;
}

void Reverb::CombFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    filterStore = 0.0f;
    index = 0;
}

float Reverb::CombFilter::process(float input, float feedback, float damp1, float damp2)
{
    float output = buffer[index];

    // One-pole lowpass in the feedback path (damping)
    filterStore = output * damp2 + filterStore * damp1;

    buffer[index] = input + filterStore * feedback;

    if (++index >= bufSize)
        index = 0;

    return output;
}

// --- AllpassFilter ---

void Reverb::AllpassFilter::setSize(int size)
{
    bufSize = size;
    buffer.resize(size, 0.0f);
    index = 0;
}

void Reverb::AllpassFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    index = 0;
}

float Reverb::AllpassFilter::process(float input)
{
    float bufOut = buffer[index];
    float output = bufOut - input;

    buffer[index] = input + bufOut * kFeedback;

    if (++index >= bufSize)
        index = 0;

    return output;
}

// --- Reverb ---

Reverb::Reverb()
{
    // Prepare with default sample rate so default-constructed reverb works
    prepare(44100.0f);
}

void Reverb::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;

    for (int i = 0; i < kNumCombs; ++i)
    {
        int len = scaleTuning(kCombTunings[i], sampleRate);
        combL_[i].setSize(len);
        combR_[i].setSize(len + kStereoSpread);
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

void Reverb::reset()
{
    for (auto& c : combL_) c.clear();
    for (auto& c : combR_) c.clear();
    for (auto& a : allpassL_) a.clear();
    for (auto& a : allpassR_) a.clear();
}

void Reverb::setSize(float size)
{
    size_ = std::clamp(size, 0.0f, 1.0f);
    updateParams();
}

void Reverb::setDamp(float damp)
{
    damp_ = std::clamp(damp, 0.0f, 1.0f);
    updateParams();
}

void Reverb::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void Reverb::setFreeze(bool freeze)
{
    freeze_ = freeze;
    updateParams();
}

void Reverb::updateParams()
{
    if (freeze_)
    {
        feedback_ = 1.0f;
        damp1_ = 0.0f;
        damp2_ = 1.0f;
    }
    else
    {
        // Map size 0–1 to feedback 0.6–0.96
        feedback_ = 0.6f + size_ * 0.36f;
        // Damp: damp1 is the LP coefficient, damp2 = 1 - damp1
        damp1_ = damp_ * 0.4f;
        damp2_ = 1.0f - damp1_;
    }
}

std::pair<float, float> Reverb::process(float input)
{
    float in = input * kInputGain;

    // Parallel comb filters
    float outL = 0.0f;
    float outR = 0.0f;

    for (int i = 0; i < kNumCombs; ++i)
    {
        outL += combL_[i].process(in, feedback_, damp1_, damp2_);
        outR += combR_[i].process(in, feedback_, damp1_, damp2_);
    }

    // Series allpass filters (diffusion)
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
