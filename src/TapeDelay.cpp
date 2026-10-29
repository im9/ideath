#include <ideath/TapeDelay.h>
#include <ideath/Saturation.h>
#include <algorithm>
#include <cmath>

namespace ideath {

static constexpr float kAntiDenormal = 1e-25f;
static constexpr float kTwoPi = 6.283185307f;

// Shortest read: 4-point Hermite needs the sample one newer than the
// interpolation interval, and the current input is written after the read.
static constexpr float kMinDelaySamples = 2.0f;

// In-loop diffuser: four Schroeder allpasses, mutually prime lengths at
// 48 kHz (1.4–4.0 ms, 10.4 ms in all).  g = kMaxDiffuseG · diffusion; at 0.7
// each stage rings for ≈ 19 × its length (T60 = 3·N / −log10 g), so a
// repeat is smeared over a few tens of ms and every pass smears it more.
static constexpr int kDiffuserLengths48k[4] = { 67, 101, 139, 191 };
static constexpr float kMaxDiffuseG = 0.7f;

static constexpr float kMaxOffsetSec = 0.05f;

// Crossfade between the straight and the diffused read when the delay
// crosses the diffuser's length: long enough that a 200 Hz tone's two
// reads (≈ 2 cycles apart) blend without a step, short against any echo.
// Linear: the two reads carry the same repeats, so they are correlated.
static constexpr float kDiffuseFadeSec = 0.005f;

// Clean-repeat ceiling: identity below kCleanKnee, tanh-shaped approach to
// 1 above it (unit slope at the knee, so no kink).
static constexpr float kCleanKnee = 0.8f;

static float cleanClip(float x)
{
    const float a = std::fabs(x);
    if (a <= kCleanKnee)
        return x;
    const float y = kCleanKnee + (1.0f - kCleanKnee) * std::tanh((a - kCleanKnee) / (1.0f - kCleanKnee));
    return std::copysign(y, x);
}

void TapeDelay::prepare(float sampleRate, float maxDelaySec)
{
    sampleRate_ = sampleRate;
    maxDelaySamples_ = std::max(kMinDelaySamples + 1.0f, maxDelaySec * sampleRate_);
    // + 4: Hermite reads two samples past the integer delay; the extra
    // keeps the oldest read clear of the write head.
    bufferSize_ = static_cast<int>(maxDelaySamples_) + 4;
    diffuserSamples_ = 0.0f;
    for (Loop* loop : { &a_, &b_ })
    {
        loop->buffer.assign(static_cast<size_t>(bufferSize_), 0.0f);
        loop->diffused.assign(static_cast<size_t>(bufferSize_), 0.0f);
        for (int k = 0; k < 4; ++k)
        {
            AllpassFilter& ap = loop->diffuser[static_cast<size_t>(k)];
            ap.prepare(sampleRate, 0.01f);
            ap.setDelay(static_cast<float>(kDiffuserLengths48k[k]) / 48000.0f);
        }
    }
    for (const AllpassFilter& ap : a_.diffuser)
        diffuserSamples_ += static_cast<float>(ap.getDelaySamples());

    updateFeedbackFilters();
    setDiffusion(diffusion_);
    setStereoOffset(offsetSec_);
    setWowRate(0.3f);
    setFlutterRate(4.0f);
    reset();
}

void TapeDelay::reset()
{
    for (Loop* loop : { &a_, &b_ })
    {
        clearLoop(*loop);
        loop->writePos = 0;
    }
    diffuseMix_ = diffuserActive() ? 1.0f : 0.0f;
    offsetHighpass_.reset();
    offsetLowpass_.reset();
    wowPhase_ = 0.0f;
    flutterPhase_ = 0.0f;
}

void TapeDelay::setDelay(float delaySec)
{
    delaySamples_ = std::clamp(delaySec * sampleRate_, kMinDelaySamples, maxDelaySamples_);
}

void TapeDelay::setFeedback(float feedback)
{
    feedback_ = std::clamp(feedback, -0.999f, 0.999f);
}

void TapeDelay::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void TapeDelay::setWowDepth(float seconds)
{
    const float maxDepth = std::min(0.05f * sampleRate_, maxDelaySamples_ * 0.25f);
    wowDepthSamples_ = std::clamp(seconds * sampleRate_, 0.0f, maxDepth);
}

void TapeDelay::setWowRate(float rateHz)
{
    rateHz = std::clamp(rateHz, 0.01f, 10.0f);
    wowInc_ = rateHz / sampleRate_;
}

void TapeDelay::setFlutterDepth(float seconds)
{
    const float maxDepth = std::min(0.01f * sampleRate_, maxDelaySamples_ * 0.1f);
    flutterDepthSamples_ = std::clamp(seconds * sampleRate_, 0.0f, maxDepth);
}

void TapeDelay::setFlutterRate(float rateHz)
{
    rateHz = std::clamp(rateHz, 0.1f, 20.0f);
    flutterInc_ = rateHz / sampleRate_;
}

void TapeDelay::setLowpass(float freqHz)
{
    // Hosts call setters every sample; recompute the six biquads only on
    // a change.
    if (freqHz == lowpassHz_)
        return;
    lowpassHz_ = freqHz;
    updateFeedbackFilters();
}

void TapeDelay::setHighpass(float freqHz)
{
    if (freqHz == highpassHz_)
        return;
    highpassHz_ = freqHz;
    updateFeedbackFilters();
}

void TapeDelay::setDrive(float drive)
{
    drive_ = std::max(1.0f, drive);
}

void TapeDelay::setSaturation(bool enabled)
{
    saturation_ = enabled;
}

void TapeDelay::setDiffusion(float amount)
{
    diffusion_ = std::clamp(amount, 0.0f, 1.0f);
    const float g = kMaxDiffuseG * diffusion_;
    for (Loop* loop : { &a_, &b_ })
        for (AllpassFilter& ap : loop->diffuser)
            ap.setFeedback(g);
}

void TapeDelay::setStereoMode(StereoMode mode)
{
    // The right loop only runs in PingPong; clear it on entry so a tail
    // left from an earlier PingPong stretch does not replay.
    if (mode == StereoMode::PingPong && stereoMode_ != StereoMode::PingPong)
        clearLoop(b_);
    stereoMode_ = mode;
}

void TapeDelay::clearLoop(Loop& loop)
{
    std::fill(loop.buffer.begin(), loop.buffer.end(), 0.0f);
    std::fill(loop.diffused.begin(), loop.diffused.end(), 0.0f);
    loop.highpass.reset();
    loop.lowpass.reset();
    for (AllpassFilter& ap : loop.diffuser)
        ap.reset();
}

void TapeDelay::setStereoOffset(float seconds)
{
    // Kept in seconds too, so prepare() at another rate keeps the time.
    offsetSec_ = std::clamp(seconds, 0.0f, kMaxOffsetSec);
    offsetSamples_ = offsetSec_ * sampleRate_;
}

void TapeDelay::updateFeedbackFilters()
{
    const float hp = std::clamp(highpassHz_, 5.0f, sampleRate_ * 0.45f);
    const float lp = std::clamp(lowpassHz_, hp + 5.0f, sampleRate_ * 0.45f);
    for (Loop* loop : { &a_, &b_ })
    {
        loop->highpass.setHighpass(hp, 0.707f, sampleRate_);
        loop->lowpass.setLowpass(lp, 0.707f, sampleRate_);
    }
    offsetHighpass_.setHighpass(hp, 0.707f, sampleRate_);
    offsetLowpass_.setLowpass(lp, 0.707f, sampleRate_);
}

float TapeDelay::readDelay(const std::vector<float>& buffer, int writePos,
                           float delaySamples) const
{
    // 4-point cubic Hermite (Catmull-Rom).  A linear read at a fractional
    // delay is a low-pass applied once per repeat (|cos(πf/sr)| at fraction
    // 0.5: −1.5 dB per pass at 8 kHz), so clean repeats would dull
    // audibly; Hermite loses 0.3 dB there.  Keep the sub-sample fraction at its
    // native small magnitude; wrap indices in int arithmetic.
    const int delayInt = static_cast<int>(delaySamples);
    const float f = delaySamples - static_cast<float>(delayInt);

    auto at = [&](int age) {
        int idx = writePos - age;
        if (idx < 0)
            idx += bufferSize_;
        return buffer[static_cast<size_t>(idx)];
    };
    const float ym1 = at(delayInt - 1);
    const float y0 = at(delayInt);
    const float y1 = at(delayInt + 1);
    const float y2 = at(delayInt + 2);
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

float TapeDelay::color(Loop& loop, float wet)
{
    // Tape playback coloring on every head read (first echo included).
    float colored = loop.highpass.process(wet);
    colored = loop.lowpass.process(colored);
    return saturation_ ? Saturation::tanhDrive(colored, drive_) : cleanClip(colored);
}

float TapeDelay::loopRead(const Loop& loop, float delay) const
{
    // Each loop is written twice: straight, and through the diffuser (an
    // allpass chain of total length diffuserSamples_).  An allpass's group
    // delay averaged over frequency equals its length for any g, so reading
    // the diffused copy that much sooner puts each smeared repeat's energy
    // centroid at the asked-for time (at g = 0 the chain is a pure delay:
    // the echo is exactly where it was).  When the delay (at its modulation
    // trough) is shorter than the diffuser, the straight copy is read
    // instead; crossing that threshold crossfades the two reads over
    // kDiffuseFadeSec rather than jumping the read point by ≈ 10 ms.
    const float direct = std::max(kMinDelaySamples, delay);
    if (diffuseMix_ <= 0.0f)
        return readDelay(loop.buffer, loop.writePos, direct);
    const float shortened = std::max(kMinDelaySamples, delay - diffuserSamples_);
    const float diffused = readDelay(loop.diffused, loop.writePos, shortened);
    if (diffuseMix_ >= 1.0f)
        return diffused;
    const float straight = readDelay(loop.buffer, loop.writePos, direct);
    return straight + diffuseMix_ * (diffused - straight);
}

bool TapeDelay::diffuserActive() const
{
    return delaySamples_ - wowDepthSamples_ - flutterDepthSamples_
           >= diffuserSamples_ + kMinDelaySamples;
}

void TapeDelay::writeLoop(Loop& loop, float value)
{
    float v = value;
    for (AllpassFilter& ap : loop.diffuser)
        v = ap.process(v);
    loop.buffer[static_cast<size_t>(loop.writePos)] = value + kAntiDenormal;
    loop.diffused[static_cast<size_t>(loop.writePos)] = v + kAntiDenormal;
    if (++loop.writePos >= bufferSize_)
        loop.writePos = 0;
}

void TapeDelay::advanceLfos(float& wow, float& flutter, float* wowQ, float* flutterQ)
{
    wow = std::sin(kTwoPi * wowPhase_);
    flutter = std::sin(kTwoPi * flutterPhase_);
    if (wowQ)
    {
        *wowQ = std::cos(kTwoPi * wowPhase_);
        *flutterQ = std::cos(kTwoPi * flutterPhase_);
    }

    wowPhase_ += wowInc_;
    flutterPhase_ += flutterInc_;
    wowPhase_ -= std::floor(wowPhase_);
    flutterPhase_ -= std::floor(flutterPhase_);
}

float TapeDelay::process(float input)
{
    return run(input, false).first;
}

std::pair<float, float> TapeDelay::processStereo(float input)
{
    return run(input, true);
}

std::pair<float, float> TapeDelay::run(float input, bool stereo)
{
    // One code path for mono and stereo, so in Offset mode the left channel
    // of processStereo() is bit-identical to process() (in PingPong the left
    // loop is fed from the right one instead of itself).

    // Move toward the diffused or the straight read (see loopRead).
    const float target = diffuserActive() ? 1.0f : 0.0f;
    const float fadeStep = 1.0f / (kDiffuseFadeSec * sampleRate_);
    diffuseMix_ = target > diffuseMix_ ? std::min(target, diffuseMix_ + fadeStep)
                                       : std::max(target, diffuseMix_ - fadeStep);

    float wow, flutter, wowQ = 0.0f, flutterQ = 0.0f;
    advanceLfos(wow, flutter, stereo ? &wowQ : nullptr, &flutterQ);

    const float delayL = std::clamp(delaySamples_ + wow * wowDepthSamples_
                                        + flutter * flutterDepthSamples_,
                                    kMinDelaySamples, maxDelaySamples_);
    const float dry = input * (1.0f - mix_);
    const float coloredL = color(a_, loopRead(a_, delayL));

    if (!stereo)
    {
        writeLoop(a_, input + coloredL * feedback_);
        return { dry + coloredL * mix_, 0.0f };
    }

    // Right channel: wow/flutter in quadrature, so the modulation also
    // decorrelates the two sides.
    const float delayR = std::clamp(delaySamples_ + wowQ * wowDepthSamples_
                                        + flutterQ * flutterDepthSamples_,
                                    kMinDelaySamples, maxDelaySamples_);

    if (stereoMode_ == StereoMode::PingPong)
    {
        // Two loops cross-fed: input enters the left loop, each loop's
        // repeat is written into the other → echoes alternate L, R, L, …
        const float coloredR = color(b_, loopRead(b_, delayR));
        writeLoop(a_, input + coloredR * feedback_);
        writeLoop(b_, coloredL * feedback_);
        return { dry + coloredL * mix_, dry + coloredR * mix_ };
    }

    // Offset: one loop (the mono one), right channel read stereoOffset
    // later through its own coloring filters.
    float coloredR = offsetHighpass_.process(
        loopRead(a_, std::min(delayR + offsetSamples_, maxDelaySamples_)));
    coloredR = offsetLowpass_.process(coloredR);
    coloredR = saturation_ ? Saturation::tanhDrive(coloredR, drive_) : cleanClip(coloredR);

    writeLoop(a_, input + coloredL * feedback_);
    return { dry + coloredL * mix_, dry + coloredR * mix_ };
}

} // namespace ideath
