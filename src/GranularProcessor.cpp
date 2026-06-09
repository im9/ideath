#include <ideath/GranularProcessor.h>

#include <algorithm>
#include <cmath>

namespace ideath {

void GranularProcessor::prepare(float sampleRate, int bufferLengthSamples)
{
    // SR must be positive; buffer must be long enough that the modulo
    // arithmetic in renderGrain() is well-defined.  16 samples is an
    // arbitrary floor — anything shorter than a grain is unusable
    // musically, and we want to avoid 0-length corner cases.
    sampleRate_ = std::max(sampleRate, 1.0f);
    bufferSize_ = std::max(bufferLengthSamples, 16);
    buffer_.assign(static_cast<size_t>(bufferSize_), 0.0f);
    reset();
}

void GranularProcessor::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_   = 0;
    spawnTimer_ = 0.0f;
    for (auto& g : grains_)
        g.active = false;
    // Deterministic seed — two instances reset to the same state.
    rngState_ = kRngSeed;
}

void GranularProcessor::writeSample(float input)
{
    if (frozen_)
        return;
    if (bufferSize_ <= 0)
        return;
    buffer_[static_cast<size_t>(writePos_)] = input;
    ++writePos_;
    if (writePos_ >= bufferSize_)
        writePos_ = 0;
}

void GranularProcessor::setGrainRate(float hz)
{
    // Clamp to [0, sampleRate].  Negative becomes 0 (no spawn).  Above
    // sampleRate the spawn-per-sample saturates the pool; capping at SR
    // avoids surprising behaviour from grainRate > sampleRate (which would
    // otherwise be reinterpreted as 0 by 1/SR step).
    grainRate_ = std::clamp(hz, 0.0f, sampleRate_);
}

void GranularProcessor::setGrainSize(float seconds)
{
    // 0.1 ms floor matches the test-suite expectation that 1 ms grains are
    // legal but degenerate; 1 s ceiling is musically reasonable and avoids
    // grains that outlive the buffer.
    grainSize_ = std::clamp(seconds, 1e-4f, 1.0f);
}

void GranularProcessor::setPitchSpread(float semitones)
{
    // Allow up to ±24 semitones — two octaves either side.  Beyond that
    // the varispeed read aliases badly and grain envelopes start
    // overlapping in ways that no longer sound like granular synthesis.
    pitchSpread_ = std::clamp(semitones, 0.0f, 24.0f);
}

void GranularProcessor::setPositionScatter(float amount)
{
    positionScatter_ = std::clamp(amount, 0.0f, 1.0f);
}

void GranularProcessor::setFreeze(bool on)
{
    frozen_ = on;
}

int GranularProcessor::activeGrainCount() const
{
    int n = 0;
    for (const auto& g : grains_)
        if (g.active) ++n;
    return n;
}

// xorshift32, matching ideath::Noise — same algorithm, separated state so
// we don't disturb Noise instances elsewhere.
float GranularProcessor::nextUniform01()
{
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    constexpr float kScale = 1.0f / 4294967296.0f;  // 1 / 2^32
    return static_cast<float>(rngState_) * kScale;
}

float GranularProcessor::nextUniformPM1()
{
    return nextUniform01() * 2.0f - 1.0f;
}

void GranularProcessor::trySpawnGrain()
{
    if (bufferSize_ <= 0) return;
    if (grainRate_ <= 0.0f) return;

    // Find first inactive slot.  If none, drop the spawn silently — see
    // header comment on pool exhaustion.
    Grain* slot = nullptr;
    for (auto& g : grains_)
    {
        if (!g.active)
        {
            slot = &g;
            break;
        }
    }
    if (!slot) return;

    // Pitch: ratio = 2^(semis/12), semis ∈ [-pitchSpread, +pitchSpread].
    const float semis = nextUniformPM1() * pitchSpread_;
    const float pitch = std::pow(2.0f, semis * (1.0f / 12.0f));

    // Read start position: a uniform offset in [0, scatter · bufferSize)
    // samples *behind* the current write head.  Wrap into [0, bufferSize).
    const float u = nextUniform01();
    const float offset = u * positionScatter_ * static_cast<float>(bufferSize_);
    float readPos = static_cast<float>(writePos_) - offset;
    // Modulo into [0, bufferSize_) — keep it as float for sub-sample
    // interp.  std::floor on a negative float gives the correct positive
    // remainder after the add-back.
    const float bs = static_cast<float>(bufferSize_);
    readPos -= bs * std::floor(readPos / bs);
    if (readPos < 0.0f) readPos += bs;     // guard against fp drift
    if (readPos >= bs)  readPos -= bs;

    // Grain size in samples; envelope advances 1/N per sample.
    const float grainSamples = std::max(grainSize_ * sampleRate_, 1.0f);

    slot->active   = true;
    slot->readPos  = readPos;
    slot->pitch    = pitch;
    slot->envPhase = 0.0f;
    slot->envInc   = 1.0f / grainSamples;
}

float GranularProcessor::renderGrain(Grain& g) const
{
    // Linear-interpolated ring-buffer read at g.readPos.
    const float bs = static_cast<float>(bufferSize_);
    int i0 = static_cast<int>(g.readPos);
    // Robust against fp edge cases where readPos sits a touch below 0 or
    // a touch above bufferSize from upstream arithmetic.
    if (i0 < 0)              i0 = ((i0 % bufferSize_) + bufferSize_) % bufferSize_;
    else if (i0 >= bufferSize_) i0 %= bufferSize_;
    int i1 = i0 + 1;
    if (i1 >= bufferSize_) i1 -= bufferSize_;
    const float frac = g.readPos - std::floor(g.readPos);
    const float s0 = buffer_[static_cast<size_t>(i0)];
    const float s1 = buffer_[static_cast<size_t>(i1)];
    const float sample = s0 + frac * (s1 - s0);

    // Hann window over envPhase ∈ [0, 1):  0.5 − 0.5·cos(2π·envPhase).
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float env = 0.5f - 0.5f * std::cos(kTwoPi * g.envPhase);

    // Advance read pointer (varispeed) and envelope.
    g.readPos += g.pitch;
    // Wrap read pos into [0, bufferSize_).  std::floor on the quotient
    // gives the correct positive remainder for both positive and negative
    // readPos.  The two trailing single-step adjustments guard against
    // float-rounding leaving readPos pinned exactly at bs or one ULP below
    // 0 (which would otherwise drift indices over time).
    if (g.readPos >= bs || g.readPos < 0.0f)
        g.readPos -= bs * std::floor(g.readPos / bs);
    if (g.readPos >= bs)  g.readPos -= bs;
    if (g.readPos < 0.0f) g.readPos += bs;

    g.envPhase += g.envInc;
    if (g.envPhase >= 1.0f)
        g.active = false;

    return sample * env;
}

float GranularProcessor::process()
{
    if (bufferSize_ <= 0)
        return 0.0f;

    // Spawn timer.  Counts down by one sample period each call.  When it
    // crosses zero, launch a grain (if pool has room) and reset by
    // 1/grainRate.  We may need to spawn multiple grains per call if the
    // timer was overdrawn (e.g. very high grainRate); cap the inner loop
    // at kMaxGrains to bound CPU and silently drop excess spawns.
    if (grainRate_ > 0.0f)
    {
        spawnTimer_ -= 1.0f / sampleRate_;
        int safety = kMaxGrains;
        while (spawnTimer_ <= 0.0f && safety-- > 0)
        {
            trySpawnGrain();
            spawnTimer_ += 1.0f / grainRate_;
        }
        // If the timer is still negative after kMaxGrains spawns this call
        // (grainRate ≫ sampleRate / kMaxGrains), clamp it forward to keep
        // future calls cheap.
        if (spawnTimer_ < 0.0f)
            spawnTimer_ = 1.0f / grainRate_;
    }

    // Sum active grains.
    float sum = 0.0f;
    for (auto& g : grains_)
    {
        if (g.active)
            sum += renderGrain(g);
    }

    // Gain compensation — see test_GranularProcessor.cpp threshold
    // derivations.  O = grainRate · grainSize is the expected overlap;
    // 0.5 is the Hann mean; sqrt() targets power-preserving sum for
    // decorrelated grains, with a 0.5 floor so sparse-grain regimes
    // don't blow up.
    const float overlap = grainRate_ * grainSize_;
    const float denom = std::max(overlap * 0.5f, 0.5f);
    const float gain = 1.0f / std::sqrt(denom);

    return sum * gain;
}

} // namespace ideath
