#include <ideath/HallReverb.h>
#include <algorithm>
#include <cmath>

namespace ideath {

// Size → T60 end points.  Exponential in between so equal steps of size
// multiply the decay by the same ratio (60^0.05 ≈ 1.23 per 0.05 step).
static constexpr float kSizeMinT60 = 0.5f;
static constexpr float kSizeMaxT60 = 30.0f;

float HallReverb::sizeToT60(float size)
{
    return kSizeMinT60 * std::pow(kSizeMaxT60 / kSizeMinT60, std::clamp(size, 0.0f, 1.0f));
}

HallReverb::HallReverb()
{
    prepare(44100.0f);
}

void HallReverb::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    int size = 1;
    while (size < static_cast<int>(std::ceil(kMaxPreDelay * sampleRate)) + 1)
        size <<= 1;
    preDelay_.assign(static_cast<size_t>(size), 0.0f);
    preDelayMask_ = size - 1;

    preDelaySamples_ = static_cast<int>(preDelaySec_ * sampleRate);
    tank_.prepare(sampleRate);
    tank_.setDecay(sizeToT60(size_));
    reset();
}

void HallReverb::reset()
{
    std::fill(preDelay_.begin(), preDelay_.end(), 0.0f);
    preDelayWrite_ = 0;
    tank_.reset();
}

void HallReverb::setSize(float size)
{
    // Hosts call setters every sample; skip the pow() when nothing moved.
    const float s = std::clamp(size, 0.0f, 1.0f);
    if (s == size_)
        return;
    size_ = s;
    tank_.setDecay(sizeToT60(size_));
}

void HallReverb::setDamp(float damp)
{
    tank_.setDamp(damp);
}

void HallReverb::setPreDelay(float seconds)
{
    const float s = std::clamp(seconds, 0.0f, kMaxPreDelay);
    // Seconds are kept so prepare() at another rate keeps the time.
    preDelaySec_ = s;
    preDelaySamples_ = static_cast<int>(s * sampleRate_);
}

void HallReverb::setModDepth(float depth)
{
    tank_.setModDepth(depth);
}

void HallReverb::setModRate(float rateHz)
{
    tank_.setModRate(rateHz);
}

void HallReverb::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void HallReverb::setFreeze(bool freeze)
{
    freeze_ = freeze;
    tank_.setFreeze(freeze);
}

std::pair<float, float> HallReverb::process(float input)
{
    preDelay_[static_cast<size_t>(preDelayWrite_)] = input;
    const float delayed =
        preDelay_[static_cast<size_t>((preDelayWrite_ - preDelaySamples_) & preDelayMask_)];
    preDelayWrite_ = (preDelayWrite_ + 1) & preDelayMask_;

    // Frozen tank is lossless: anything fed now would stay forever and
    // pile up, so the held tail gets silence (only the input diffuser's
    // last ≈ 30 ms drains in after the switch).
    auto [l, r] = tank_.process(freeze_ ? 0.0f : delayed);

    const float dry = input * (1.0f - mix_);
    return { dry + l * mix_, dry + r * mix_ };
}

} // namespace ideath
