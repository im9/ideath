#include <ideath/Chorus.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ideath {

// Denormal DC injection for the linear feedback state (same value used
// across the library, see CLAUDE.md).
static constexpr float kAntiDenormal = 1e-25f;
static constexpr float kTwoPi        = 6.28318530717958647692f;

// Fixed chorus base delay in seconds.  Sits in ambient-chorus territory
// (10–15 ms per CLAUDE.md § design notes).  Depth adds ± up to 15 ms;
// max instantaneous delay 12 + 15 = 27 ms → buffer sized to 40 ms for
// safe headroom + linear-interpolation lookahead.
static constexpr float kBaseDelaySec = 0.012f;
static constexpr float kMaxDelaySec  = 0.040f;

// Depth ceiling (ms → seconds).  Matches the API doc string.
static constexpr float kMaxDepthMs   = 15.0f;

void Chorus::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;

    bufferSize_ = static_cast<int>(kMaxDelaySec * sampleRate_) + 4;
    buffer_.assign(static_cast<size_t>(bufferSize_), 0.0f);
    writePos_ = 0;

    baseDelaySamples_ = kBaseDelaySec * sampleRate_;

    // Re-derive dependent samples-domain params from stored SI-unit values,
    // in case setters were called before prepare() with a different sr.
    setRate(rateHz_);
    setDepth(depthSamples_ / sampleRate_ * 1000.0f);

    reset();
}

void Chorus::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    phase_    = 0.0f;
}

void Chorus::setVoiceCount(int n)
{
    voiceCount_ = std::clamp(n, 1, kMaxVoices);
}

void Chorus::setRate(float hz)
{
    // Clamp to a musically useful LFO range.  Upper bound of 20 Hz keeps the
    // modulation firmly sub-audio (higher rates start producing audible
    // sidebands rather than "wobble").
    rateHz_   = std::clamp(hz, 0.01f, 20.0f);
    phaseInc_ = rateHz_ / sampleRate_;
}

void Chorus::setDepth(float milliseconds)
{
    const float ms = std::clamp(milliseconds, 0.0f, kMaxDepthMs);
    depthSamples_  = ms * 0.001f * sampleRate_;
}

void Chorus::setSpread(float amount)
{
    spread_ = std::clamp(amount, 0.0f, 1.0f);
}

void Chorus::setFeedback(float amount)
{
    feedback_ = std::clamp(amount, -0.9f, 0.9f);
}

void Chorus::setMix(float wet)
{
    mix_ = std::clamp(wet, 0.0f, 1.0f);
}

float Chorus::readAt(float delaySamples) const
{
    // Integer + fractional split, wrap indices with int arithmetic so the
    // sub-sample fraction keeps its full float precision — same approach as
    // DelayLine::readDelay (see src/DelayLine.cpp comment).
    const int   delayInt = static_cast<int>(delaySamples);
    const float frac     = delaySamples - static_cast<float>(delayInt);

    int idxRecent = writePos_ - delayInt;
    if (idxRecent < 0) idxRecent += bufferSize_;
    int idxOlder = idxRecent - 1;
    if (idxOlder < 0) idxOlder += bufferSize_;

    const float a = buffer_[static_cast<size_t>(idxRecent)];
    const float b = buffer_[static_cast<size_t>(idxOlder)];
    return a + frac * (b - a);
}

float Chorus::voiceDelay(int i) const
{
    // Per-voice LFO phase offset: `spread * i / voiceCount` fraction of a
    // full cycle.  `spread = 0` collapses all voices to phase = phase_
    // (identical modulation); `spread = 1` spreads them uniformly around
    // the LFO cycle.
    const float voicePhaseOffset =
        (voiceCount_ > 1) ? spread_ * static_cast<float>(i) / static_cast<float>(voiceCount_) : 0.0f;
    float p = phase_ + voicePhaseOffset;
    p -= std::floor(p);

    const float lfo = std::sin(kTwoPi * p);   // [-1, 1]

    // Clamp so the read tap always stays inside [1, bufferSize−2] even at
    // extreme depth/base combos.  Buffer sizing guarantees headroom but the
    // clamp defends the interpolation window against off-by-one at seams.
    float d = baseDelaySamples_ + depthSamples_ * lfo;
    d = std::clamp(d, 1.0f, static_cast<float>(bufferSize_ - 2));
    return d;
}

float Chorus::processMono(float in)
{
    // Advance LFO first so the write and reads sample the same phase.
    phase_ += phaseInc_;
    phase_ -= std::floor(phase_);

    // Sum N voices → wet bus (mean, so amplitude is bounded regardless of
    // voice count).  Feedback then loops the wet bus back into the write.
    float wet = 0.0f;
    for (int i = 0; i < voiceCount_; ++i)
        wet += readAt(voiceDelay(i));
    wet *= (1.0f / static_cast<float>(voiceCount_));

    // Denormal-safe feedback write.  Feedback path is the only linear-state
    // recirculation in this primitive; DC injection matches the library
    // convention (see CLAUDE.md § Denormal protection).
    buffer_[static_cast<size_t>(writePos_)] = in + wet * feedback_ + kAntiDenormal;

    if (++writePos_ >= bufferSize_)
        writePos_ = 0;

    return in * (1.0f - mix_) + wet * mix_;
}

void Chorus::processStereo(float in, float& outL, float& outR)
{
    // Advance LFO once per sample (same phase for both channels).
    phase_ += phaseInc_;
    phase_ -= std::floor(phase_);

    // Per-voice pan position: voice i pans to `t = i/(N−1) ∈ [0,1]`, then
    // equal-power law `gainL = cos(π/2 · t)`, `gainR = sin(π/2 · t)` keeps
    // the mono sum energy-constant regardless of N.  Single-voice case sits
    // dead centre.
    float wetL   = 0.0f;
    float wetR   = 0.0f;
    float wetSum = 0.0f;  // used for feedback tap (mono-sum semantics match processMono)
    for (int i = 0; i < voiceCount_; ++i)
    {
        const float v = readAt(voiceDelay(i));
        wetSum += v;

        float panT;
        if (voiceCount_ == 1)
            panT = 0.5f; // centred
        else
            panT = static_cast<float>(i) / static_cast<float>(voiceCount_ - 1);

        const float gL = std::cos(0.5f * static_cast<float>(M_PI) * panT);
        const float gR = std::sin(0.5f * static_cast<float>(M_PI) * panT);
        wetL += v * gL;
        wetR += v * gR;
    }
    // Normalise the panned sums by 1/N to match processMono's mean-of-voices
    // scaling — this keeps the two channels bounded regardless of voice count
    // and ensures processStereo(x, L, R) with spread = 0 collapses to
    // L ≈ R ≈ processMono(x) up to the per-voice equal-power pan weighting.
    const float invN = 1.0f / static_cast<float>(voiceCount_);
    wetL   *= invN;
    wetR   *= invN;
    wetSum *= invN;

    // Feedback tap: use the mono sum so channel imbalance can't feed
    // asymmetric build-up back into the loop.
    buffer_[static_cast<size_t>(writePos_)] = in + wetSum * feedback_ + kAntiDenormal;

    if (++writePos_ >= bufferSize_)
        writePos_ = 0;

    outL = in * (1.0f - mix_) + wetL * mix_;
    outR = in * (1.0f - mix_) + wetR * mix_;
}

} // namespace ideath
