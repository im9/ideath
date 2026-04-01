#include <ideath/ShimmerReverb.h>
#include <algorithm>
#include <cmath>

namespace ideath {

static constexpr float kTwoPi = 6.283185307f;
static constexpr float kPi = 3.141592654f;

// --- ModAllpass ---

void ShimmerReverb::ModAllpass::prepare(int size, float fb, float lfoRate,
                                         float phase, float sampleRate)
{
    bufSize = size + 64; // headroom for modulation
    buffer.resize(bufSize, 0.0f);
    writeIndex = 0;
    feedback = fb;
    lfoPhase = phase;
    lfoInc = lfoRate / sampleRate;
    modDepth = 49.0f; // max ±49 samples (from inboil)
}

void ShimmerReverb::ModAllpass::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}

float ShimmerReverb::ModAllpass::process(float input, float modScale)
{
    float lfo = std::sin(kTwoPi * lfoPhase) * modDepth * modScale;
    lfoPhase += lfoInc;
    if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

    int baseDelay = bufSize - 64;
    float readPos = static_cast<float>(writeIndex) - static_cast<float>(baseDelay) + lfo;
    while (readPos < 0.0f) readPos += static_cast<float>(bufSize);

    // Linear interpolation
    int idx0 = static_cast<int>(readPos) % bufSize;
    float frac = readPos - std::floor(readPos);
    int idx1 = (idx0 + 1) % bufSize;

    float bufOut = buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;

    float output = bufOut - input * feedback;
    buffer[writeIndex] = input + bufOut * feedback;

    if (++writeIndex >= bufSize)
        writeIndex = 0;

    return output;
}

// --- FixedDelay ---

void ShimmerReverb::FixedDelay::prepare(int size)
{
    bufSize = size;
    buffer.resize(size, 0.0f);
    index = 0;
}

void ShimmerReverb::FixedDelay::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    index = 0;
}

float ShimmerReverb::FixedDelay::process(float input)
{
    float output = buffer[index];
    buffer[index] = input;
    if (++index >= bufSize)
        index = 0;
    return output;
}

// --- PitchShifter ---

void ShimmerReverb::PitchShifter::prepare(int size)
{
    bufSize = size;
    buffer.resize(size, 0.0f);
    writeIndex = 0;
    phase1 = 0.0f;
    phase2 = 0.5f;
}

void ShimmerReverb::PitchShifter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    phase1 = 0.0f;
    phase2 = 0.5f;
}

float ShimmerReverb::PitchShifter::process(float input)
{
    buffer[writeIndex] = input;

    // +12 semitones = 2× speed → read pointer advances 2× as fast
    // Implemented as decreasing delay: phase advances at (ratio - 1) / windowSize
    float phaseInc = 1.0f / static_cast<float>(kWindowSize); // ratio=2, speed=1 extra

    // Tap 1
    float delay1 = phase1 * static_cast<float>(kWindowSize);
    float readPos1 = static_cast<float>(writeIndex) - delay1;
    while (readPos1 < 0.0f) readPos1 += static_cast<float>(bufSize);
    int ri1 = static_cast<int>(readPos1) % bufSize;
    float frac1 = readPos1 - std::floor(readPos1);
    int ri1b = (ri1 + 1) % bufSize;
    float s1 = buffer[ri1] * (1.0f - frac1) + buffer[ri1b] * frac1;
    float env1 = 0.5f - 0.5f * std::cos(kTwoPi * phase1); // Hann window

    // Tap 2 (50% offset)
    float delay2 = phase2 * static_cast<float>(kWindowSize);
    float readPos2 = static_cast<float>(writeIndex) - delay2;
    while (readPos2 < 0.0f) readPos2 += static_cast<float>(bufSize);
    int ri2 = static_cast<int>(readPos2) % bufSize;
    float frac2 = readPos2 - std::floor(readPos2);
    int ri2b = (ri2 + 1) % bufSize;
    float s2 = buffer[ri2] * (1.0f - frac2) + buffer[ri2b] * frac2;
    float env2 = 0.5f - 0.5f * std::cos(kTwoPi * phase2);

    // Advance phases
    phase1 += phaseInc;
    if (phase1 >= 1.0f) phase1 -= 1.0f;
    phase2 += phaseInc;
    if (phase2 >= 1.0f) phase2 -= 1.0f;

    if (++writeIndex >= bufSize)
        writeIndex = 0;

    return s1 * env1 + s2 * env2;
}

// --- DcBlocker ---

void ShimmerReverb::DcBlocker::prepare(float sampleRate)
{
    // HPF at 80 Hz: coeff = exp(-2π × 80 / sr)
    coeff = std::exp(-kTwoPi * 80.0f / sampleRate);
    prevIn = 0.0f;
    prevOut = 0.0f;
}

void ShimmerReverb::DcBlocker::clear()
{
    prevIn = 0.0f;
    prevOut = 0.0f;
}

float ShimmerReverb::DcBlocker::process(float input)
{
    float output = input - prevIn + coeff * prevOut;
    prevIn = input;
    prevOut = output;
    return output;
}

// --- ShimmerReverb ---

// Allpass delay tunings (from inboil / Faust shimmer.dsp)
// Forward AP
static constexpr int kFwdApL[] = { 601, 613 };
static constexpr int kFwdApR[] = { 2043, 2087 };
// Feedback AP (cross-coupled)
static constexpr int kFbApL[] = { 2337, 2377 }; // fed from R
static constexpr int kFbApR[] = { 1087, 1113 }; // fed from L
// Forward AP feedback coefficients (scaled by diffusion=0.5)
static constexpr float kFwdApFb[] = { 0.35f, 0.375f }; // 0.7×0.5, 0.75×0.5
// Feedback AP feedback coefficients (asymmetric)
static constexpr float kFbApFbL[] = { 0.35f, 0.20f }; // 0.7×0.5, 0.4×0.5
static constexpr float kFbApFbR[] = { 0.35f, 0.20f };
// Fixed delay lengths in feedback paths
static constexpr int kFbDelayPreL = 4325;
static constexpr int kFbDelayPostL = 2969;
static constexpr int kFbDelayPreR = 4763;
static constexpr int kFbDelayPostR = 3111;
// LFO rates per allpass stage
static constexpr float kLfoRates[] = { 1.0f, 1.5f, 0.7f, 1.3f };
static constexpr float kLfoPhases[] = { 0.0f, 0.25f, 0.5f, 0.75f };

static int scaleDelay(int base, float sampleRate)
{
    return std::max(1, static_cast<int>(base * sampleRate / 44100.0f));
}

ShimmerReverb::ShimmerReverb()
{
    prepare(44100.0f);
}

void ShimmerReverb::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;

    // Forward allpass — L
    for (int i = 0; i < 2; ++i)
        fwdApL_[i].prepare(scaleDelay(kFwdApL[i], sampleRate), kFwdApFb[i],
                           kLfoRates[i], kLfoPhases[i], sampleRate);
    // Forward allpass — R
    for (int i = 0; i < 2; ++i)
        fwdApR_[i].prepare(scaleDelay(kFwdApR[i], sampleRate), kFwdApFb[i],
                           kLfoRates[i + 2], kLfoPhases[i + 2], sampleRate);

    // Feedback allpass — L (fed from R)
    for (int i = 0; i < 2; ++i)
        fbApL_[i].prepare(scaleDelay(kFbApL[i], sampleRate), kFbApFbL[i],
                          kLfoRates[i], kLfoPhases[i] + 0.1f, sampleRate);
    // Feedback allpass — R (fed from L)
    for (int i = 0; i < 2; ++i)
        fbApR_[i].prepare(scaleDelay(kFbApR[i], sampleRate), kFbApFbR[i],
                          kLfoRates[i + 2], kLfoPhases[i + 2] + 0.1f, sampleRate);

    // HF damping
    dampL_.clear();
    dampR_.clear();

    // DC blockers
    dcL_.prepare(sampleRate);
    dcR_.prepare(sampleRate);

    // Fixed delays
    fbDelayPreL_.prepare(scaleDelay(kFbDelayPreL, sampleRate));
    fbDelayPostL_.prepare(scaleDelay(kFbDelayPostL, sampleRate));
    fbDelayPreR_.prepare(scaleDelay(kFbDelayPreR, sampleRate));
    fbDelayPostR_.prepare(scaleDelay(kFbDelayPostR, sampleRate));

    // Pitch shifters (buffer = 2 × window for safety)
    pitchL_.prepare(PitchShifter::kWindowSize * 2);
    pitchR_.prepare(PitchShifter::kWindowSize * 2);

    fbL_ = 0.0f;
    fbR_ = 0.0f;

    // Freeze reverb (Freeverb fallback)
    freezeReverb_.prepare(sampleRate);
    freezeXfade_ = 0.0f;
    freezeXfadeInc_ = 1.0f / (0.2f * sampleRate); // 200ms crossfade

    updateParams();
    reset();
}

void ShimmerReverb::reset()
{
    for (auto& ap : fwdApL_) ap.clear();
    for (auto& ap : fwdApR_) ap.clear();
    for (auto& ap : fbApL_) ap.clear();
    for (auto& ap : fbApR_) ap.clear();

    dampL_.clear();
    dampR_.clear();
    dcL_.clear();
    dcR_.clear();

    fbDelayPreL_.clear();
    fbDelayPostL_.clear();
    fbDelayPreR_.clear();
    fbDelayPostR_.clear();

    pitchL_.clear();
    pitchR_.clear();

    fbL_ = 0.0f;
    fbR_ = 0.0f;

    freezeReverb_.reset();
    freezeXfade_ = 0.0f;
}

void ShimmerReverb::setSize(float size)
{
    size_ = std::clamp(size, 0.0f, 1.0f);
    updateParams();
}

void ShimmerReverb::setDamp(float damp)
{
    damp_ = std::clamp(damp, 0.0f, 1.0f);
    updateParams();
}

void ShimmerReverb::setShimmer(float amount)
{
    shimmer_ = std::clamp(amount, 0.0f, 1.0f);
    updateParams();
}

void ShimmerReverb::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void ShimmerReverb::setFreeze(bool freeze)
{
    if (freeze && !prevFreeze_)
    {
        // Entering freeze: configure Freeverb to hold the tail
        freezeReverb_.setSize(size_);
        freezeReverb_.setDamp(0.0f);
        freezeReverb_.setFreeze(true);
        freezeReverb_.setMix(1.0f);
    }
    else if (!freeze && prevFreeze_)
    {
        // Leaving freeze: unfreeze Freeverb
        freezeReverb_.setFreeze(false);
    }
    prevFreeze_ = freeze;
    freeze_ = freeze;
    updateParams();
}

void ShimmerReverb::updateParams()
{
    if (freeze_)
    {
        // Freeze: high feedback, no damping
        feedback_ = 0.35f;
        dampCoeff_ = 0.0f;
    }
    else
    {
        // Shimmer controls pitch-shifted feedback (0–0.35)
        // Size also contributes to feedback for longer decay
        feedback_ = shimmer_ * 0.35f + size_ * 0.05f;
        // Damp coefficient: 0–0.995
        dampCoeff_ = 0.005f + damp_ * 0.99f;
    }

    // Scale allpass feedback coefficients with size for longer diffusion tail
    float sizeScale = 0.5f + size_ * 0.5f; // 0.5–1.0
    for (int i = 0; i < 2; ++i)
    {
        fwdApL_[i].feedback = kFwdApFb[i] * sizeScale;
        fwdApR_[i].feedback = kFwdApFb[i] * sizeScale;
        fbApL_[i].feedback = kFbApFbL[i] * sizeScale;
        fbApR_[i].feedback = kFbApFbR[i] * sizeScale;
    }

    if (freeze_)
    {
        // Push allpass feedback higher for infinite sustain
        for (auto& ap : fwdApL_) ap.feedback = std::min(ap.feedback * 2.0f, 0.7f);
        for (auto& ap : fwdApR_) ap.feedback = std::min(ap.feedback * 2.0f, 0.7f);
        for (auto& ap : fbApL_) ap.feedback = std::min(ap.feedback * 2.0f, 0.5f);
        for (auto& ap : fbApR_) ap.feedback = std::min(ap.feedback * 2.0f, 0.5f);
    }
}

std::pair<float, float> ShimmerReverb::process(float input)
{
    // --- Shimmer path (always runs to keep state alive) ---
    float modScale = 0.5f + size_ * 2.5f;

    float fwdL = input + fbL_;
    for (auto& ap : fwdApL_)
        fwdL = ap.process(fwdL, modScale);

    float fwdR = input + fbR_;
    for (auto& ap : fwdApR_)
        fwdR = ap.process(fwdR, modScale);

    fwdL = dampL_.process(fwdL, dampCoeff_);
    fwdR = dampR_.process(fwdR, dampCoeff_);

    float dcFreeL = dcL_.process(fwdR);
    float dcFreeR = dcR_.process(fwdL);

    float fbPathL = fbDelayPreL_.process(dcFreeL);
    for (auto& ap : fbApL_)
        fbPathL = ap.process(fbPathL, modScale);
    fbPathL = fbDelayPostL_.process(fbPathL);
    fbPathL = pitchL_.process(fbPathL);

    float fbPathR = fbDelayPreR_.process(dcFreeR);
    for (auto& ap : fbApR_)
        fbPathR = ap.process(fbPathR, modScale);
    fbPathR = fbDelayPostR_.process(fbPathR);
    fbPathR = pitchR_.process(fbPathR);

    fbL_ = fbPathL * feedback_;
    fbR_ = fbPathR * feedback_;

    float shimL = fwdL;
    float shimR = fwdR;

    // --- Freeze crossfade ---
    // Feed current shimmer output into Freeverb to capture the tail
    auto [frzL, frzR] = freezeReverb_.process((shimL + shimR) * 0.5f);

    // Advance crossfade
    if (freeze_)
    {
        freezeXfade_ += freezeXfadeInc_;
        if (freezeXfade_ > 1.0f) freezeXfade_ = 1.0f;
    }
    else
    {
        freezeXfade_ -= freezeXfadeInc_;
        if (freezeXfade_ < 0.0f) freezeXfade_ = 0.0f;
    }

    // Blend: 0 = shimmer, 1 = freeverb
    float outL = shimL * (1.0f - freezeXfade_) + frzL * freezeXfade_;
    float outR = shimR * (1.0f - freezeXfade_) + frzR * freezeXfade_;

    // Dry/wet mix
    float dry = 1.0f - mix_;
    float wet = mix_;

    float left  = input * dry + outL * wet;
    float right = input * dry + outR * wet;

    return { left, right };
}

} // namespace ideath
