#include <ideath/Noise.h>

namespace ideath {

Noise::Noise(uint32_t seed)
    : state_(seed)
{
}

void Noise::reset(uint32_t seed)
{
    state_ = seed;
}

float Noise::process()
{
    // xorshift32
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;

    // Convert to float in [-1, 1].
    // Map uint32 to [0, 1] then scale to [-1, 1].
    constexpr float kScale = 2.0f / 4294967295.0f; // 2 / (2^32 - 1)
    return static_cast<float>(state_) * kScale - 1.0f;
}

} // namespace ideath
