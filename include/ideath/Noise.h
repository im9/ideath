#pragma once

#include <cstdint>

namespace ideath {

/// Fast white noise generator (xorshift32).
/// Output range: -1.0 to +1.0.
class Noise
{
public:
    explicit Noise(uint32_t seed = 0x12345678u);

    void reset(uint32_t seed = 0x12345678u);

    float process();

    uint32_t getState() const { return state_; }

private:
    uint32_t state_;
};

} // namespace ideath
