#include <ideath/LFO.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ideath {

namespace {

// --- analytical waveform helpers, all bipolar [-1, 1], phase in [0, 1) ---

inline float waveSine(float phase)
{
    return std::sin(2.0f * static_cast<float>(M_PI) * phase);
}

inline float waveTriangle(float phase)
{
    return (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
}

inline float waveSaw(float phase)
{
    return 2.0f * phase - 1.0f;
}

inline float waveSquare(float phase)
{
    return (phase < 0.5f) ? 1.0f : -1.0f;
}

// Curve morph: 0=sine, 1/3=tri, 2/3=saw, 1=square.  Implemented as three
// linear crossfade segments between the four analytical waveforms.  This is
// dirt cheap (LFO rate, no aliasing concerns) and gives perfectly predictable
// behaviour at the named anchor points.
inline float waveMorph(float curve, float phase)
{
    curve = std::clamp(curve, 0.0f, 1.0f);
    if (curve <= 1.0f / 3.0f)
    {
        const float t = curve * 3.0f; // 0..1
        return (1.0f - t) * waveSine(phase) + t * waveTriangle(phase);
    }
    if (curve <= 2.0f / 3.0f)
    {
        const float t = (curve - 1.0f / 3.0f) * 3.0f;
        return (1.0f - t) * waveTriangle(phase) + t * waveSaw(phase);
    }
    const float t = (curve - 2.0f / 3.0f) * 3.0f;
    return (1.0f - t) * waveSaw(phase) + t * waveSquare(phase);
}

// Shape ratio table: snap to a small set of musically meaningful poly-ratios
// that produce visually distinct Lissajous figures.  Index 0 (Shape=0) is
// unused — Shape=0 means "second oscillator silent" — but the table starts
// at a useful ratio for any positive Shape value.  Ratios > 1 so the second
// osc is faster than the carrier, which gives clearer beating.
constexpr float kShapeRatios[] = {
    5.0f / 4.0f,   // 5:4
    4.0f / 3.0f,   // 4:3
    3.0f / 2.0f,   // 3:2  (== 2:3)
    5.0f / 3.0f,   // 5:3
    2.0f / 1.0f,   // 2:1  (== 1:2)
    5.0f / 2.0f,   // 5:2
    3.0f / 1.0f,   // 3:1  (== 1:3)
};
constexpr int kNumShapeRatios = sizeof(kShapeRatios) / sizeof(kShapeRatios[0]);

inline float pickShapeRatio(float shape)
{
    shape = std::clamp(shape, 0.0f, 1.0f);
    int idx = static_cast<int>(shape * static_cast<float>(kNumShapeRatios));
    if (idx >= kNumShapeRatios) idx = kNumShapeRatios - 1;
    return kShapeRatios[idx];
}

} // namespace

void LFO::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void LFO::reset()
{
    phase_ = 0.0f;
    phase2_ = 0.0f;
    finished_ = false;
    holdValue_ = 0.0f;
    quantizeHold_ = 0.0f;
    prevPhase_ = 0.0f;
}

void LFO::setRate(float rateHz)
{
    // LFO rates are low but clamp to non-negative for safety.
    // No Nyquist upper bound — LFO at audio rate is a valid use case.
    rateHz = std::max(0.0f, rateHz);
    phaseInc_ = rateHz / sampleRate_;
}

void LFO::setWaveform(Waveform wf)
{
    waveform_ = wf;
}

void LFO::setPolarity(Polarity pol)
{
    polarity_ = pol;
}

void LFO::setOneShot(bool enabled)
{
    oneShot_ = enabled;
}

void LFO::setShape(float shape)
{
    shape_ = std::clamp(shape, 0.0f, 1.0f);
}

void LFO::setCurve(float curve)
{
    curve_ = std::clamp(curve, 0.0f, 1.0f);
}

void LFO::setQuantize(float quantize)
{
    quantize_ = std::clamp(quantize, 0.0f, 1.0f);
}

void LFO::trigger()
{
    phase_ = 0.0f;
    phase2_ = 0.0f;
    prevPhase_ = 0.0f;
}

float LFO::process()
{
    if (finished_)
        return (polarity_ == Polarity::Unipolar) ? (holdValue_ + 1.0f) * 0.5f : holdValue_;

    prevPhase_ = phase_;
    phase_ += phaseInc_;

    // Check for one-shot completion
    if (oneShot_ && phase_ >= 1.0f)
    {
        phase_ = 1.0f;
        finished_ = true;
    }

    phase_ -= std::floor(phase_);

    // Advance second oscillator phase (used by Shape > 0).  Doing this
    // unconditionally keeps state coherent if Shape is modulated.
    {
        const float ratio = pickShapeRatio(shape_);
        phase2_ += phaseInc_ * ratio;
        phase2_ -= std::floor(phase2_);
    }

    // --- Carrier waveform ---
    //
    // Selection priority:
    //   * If curve_ is non-default (> 0), the new sine→tri→saw→square morph
    //     drives the carrier.
    //   * Otherwise the legacy `Waveform` enum selection is used unchanged.
    //
    // This keeps every existing caller bit-equivalent while letting the new
    // continuous Curve parameter take over when callers drive it.
    float carrier = 0.0f;

    if (curve_ > 0.0f)
    {
        carrier = waveMorph(curve_, phase_);
    }
    else
    {
        switch (waveform_)
        {
            case Waveform::Sine:
                carrier = waveSine(phase_);
                break;
            case Waveform::Triangle:
                carrier = waveTriangle(phase_);
                break;
            case Waveform::Square:
                carrier = waveSquare(phase_);
                break;
            case Waveform::Saw:
                carrier = waveSaw(phase_);
                break;
            case Waveform::SampleAndHold:
                if (phase_ < prevPhase_)
                {
                    noiseState_ ^= noiseState_ << 13;
                    noiseState_ ^= noiseState_ >> 17;
                    noiseState_ ^= noiseState_ << 5;
                    constexpr float kScale = 2.0f / 4294967295.0f;
                    holdValue_ = static_cast<float>(noiseState_) * kScale - 1.0f;
                }
                carrier = holdValue_;
                break;
        }
    }

    // --- Shape: mix in second oscillator at a poly-rhythmic ratio ---
    //
    // The second oscillator uses the same morph curve so the timbral
    // character stays consistent.  Mix amount is half the Shape value so the
    // total amplitude stays bounded around ±1 (the two oscillators are
    // independent and can constructively add).
    if (shape_ > 0.0f)
    {
        const float second = (curve_ > 0.0f) ? waveMorph(curve_, phase2_)
                                              : waveSine(phase2_);
        const float mix = shape_ * 0.5f;
        carrier = (1.0f - mix) * carrier + mix * second;
    }

    // --- Quantize: lerp between smooth carrier and a once-per-cycle S&H ---
    //
    // The held value updates exactly once per LFO cycle (at phase wrap),
    // matching the spec's "1 sample hold per cycle".  The Quantize knob
    // crossfades between the smooth and held branches, so 0.5 produces the
    // documented hybrid (half slewed, half stepped).
    float out = carrier;
    if (quantize_ > 0.0f)
    {
        if (phase_ < prevPhase_ || prevPhase_ == 0.0f)
            quantizeHold_ = carrier;
        out = (1.0f - quantize_) * carrier + quantize_ * quantizeHold_;
    }

    if (finished_)
        holdValue_ = out;

    // Convert to unipolar if needed
    if (polarity_ == Polarity::Unipolar)
        out = (out + 1.0f) * 0.5f;

    return out;
}

} // namespace ideath
