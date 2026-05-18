#pragma once

#include <cstdint>

namespace ideath {

/// White noise + one-pole low-pass, with a single `Bandwidth` parameter.
///
/// At Bandwidth == 1.0 the filter is bypassed and the output is bit-exact
/// equivalent to `Noise` (same xorshift32 stream).  As Bandwidth drops the
/// cutoff sweeps logarithmically down toward ~5 Hz, taking the texture from
/// white → pink-ish → brown-ish → slow random walk.
///
/// Kept as a separate class so the bare `Noise` primitive stays a pure
/// entropy source for callers that want it.
class BandlimitedNoise
{
public:
    explicit BandlimitedNoise(uint32_t seed = 0x12345678u);

    void prepare(float sampleRate);
    void reset(uint32_t seed = 0x12345678u);

    /// 0..1, log-mapped to a one-pole LP cutoff.  1.0 = bypass (white).
    void setBandwidth(float bandwidth);

    float process();

    uint32_t getState() const { return state_; }

private:
    void recalcCoef();

    float sampleRate_ = 44100.0f;
    uint32_t state_;
    float bandwidth_ = 1.0f;
    float coef_ = 1.0f;       // one-pole LP coefficient (1.0 = bypass)
    float lpState_ = 0.0f;
};

} // namespace ideath
