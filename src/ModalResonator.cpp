#include <ideath/ModalResonator.h>
#include <algorithm>
#include <cmath>

namespace ideath {

namespace {
constexpr float kPi   = 3.14159265358979323846f;
constexpr float kLn1k = 6.9077552789821368f;  // ln(1000) — −60 dB in nats
} // namespace

ModalResonator::ModalResonator()
{
    // Default harmonic ratios: 1, 2, 3, … (integer harmonics).
    // Default decay: 0.5 s — modest bell-like ring.
    for (int i = 0; i < kMaxPartials; ++i)
    {
        ratio_[i]    = static_cast<float>(i + 1);
        decay_[i]    = 0.5f;
        qCached_[i]  = kMinQ;
        alive_[i]    = true;
        gain_[i]     = 1.0f;
    }
}

void ModalResonator::prepare(float sampleRate)
{
    sampleRate_ = (sampleRate > 0.0f) ? sampleRate : 44100.0f;

    excEnv_.prepare(sampleRate_);
    excEnv_.setDecay(kExcitationDecay);

    reset();
    updatePartialCoefficients();
}

void ModalResonator::reset()
{
    excEnv_.reset();
    for (int i = 0; i < kMaxPartials; ++i)
        partial_[i].reset();
}

void ModalResonator::setFundamental(float hz)
{
    // Clamp to [10, sr*0.45] per CLAUDE.md parameter-clamping convention.
    const float clamped = std::clamp(hz, 10.0f, sampleRate_ * 0.45f);
    // Skip the 16-BP coefficient recompute when fundamental hasn't actually
    // moved — important for the REPL hot path where setFundamental() is
    // called every sample (pitch tracking) but the value usually stays put
    // between portamento steps / modulation updates.
    if (clamped == fundamental_)
        return;
    fundamental_ = clamped;
    updatePartialCoefficients();
}

void ModalResonator::setPartialCount(int n)
{
    partialCount_ = std::clamp(n, 1, kMaxPartials);
}

void ModalResonator::setPartialRatio(int idx, float ratio)
{
    if (idx < 0 || idx >= kMaxPartials)
        return;
    // Ratio floor 0.01: ratios approaching 0 push the partial below 10 Hz
    // (the BP sanitiser's own floor), which is musically uninteresting and
    // would cause coefficient instability.  Ceiling 64: 64 × 10 Hz = 640 Hz
    // (lowest fundamental × highest ratio still well below Nyquist); higher
    // ratios are always muted by the Nyquist guard for any musical
    // fundamental.
    ratio_[idx] = std::clamp(ratio, 0.01f, 64.0f);
    updatePartial(idx);
}

void ModalResonator::setPartialDecay(int idx, float seconds)
{
    if (idx < 0 || idx >= kMaxPartials)
        return;
    // 1 ms floor / 30 s ceiling — see header comment.
    const float clamped = std::clamp(seconds, 0.001f, 30.0f);
    if (clamped == decay_[idx])
        return;
    decay_[idx] = clamped;
    // Decay drives BP Q (T60 = Q × ln(1000) / (π × fc) → Q = π × fc × T60 / ln(1000)).
    // Recompute this partial's coefficients with the new Q.
    updatePartial(idx);
}

void ModalResonator::setPartialGain(int idx, float gain)
{
    if (idx < 0 || idx >= kMaxPartials)
        return;
    // Clamp to [0, 4] — see header comment for the ceiling derivation.
    gain_[idx] = std::clamp(gain, 0.0f, 4.0f);
}

void ModalResonator::setInharmonicity(float amount)
{
    const float clamped = std::clamp(amount, 0.0f, 1.0f);
    if (clamped == inharmonicity_)
        return;
    inharmonicity_ = clamped;
    updatePartialCoefficients();
}

void ModalResonator::strike(float velocity)
{
    velocity = std::clamp(velocity, 0.0f, 1.0f);
    if (velocity <= 0.0f)
        return;
    // Fire the excitation envelope: a 2 ms noise burst that all live
    // partials see at their input.  The per-partial BPs then ring at their
    // Q-determined T60 with no additional envelope shaping — the BP is the
    // decay shape.
    excEnv_.trigger(velocity);
}

float ModalResonator::process()
{
    // --- Excitation: short noise burst, scaled by the excitation envelope.
    // After ~10 ms the envelope flushes to zero and the burst stops; the
    // partials then ring freely under their own Q-driven decays.
    float excitation = 0.0f;
    if (excEnv_.isActive())
        excitation = excNoise_.process() * excEnv_.process();

    // --- Sum every live partial.  Multiplied by Q to compensate for the
    // 0 dB-peak BP normalisation (b0 = α/a0): impulse-response peak is
    // ≈ 1/Q before the multiplication and ≈ 1 after.  See class comment
    // for the output ceiling derivation.
    float sum = 0.0f;
    for (int i = 0; i < partialCount_; ++i)
    {
        if (!alive_[i])
            continue;
        const float bp = partial_[i].process(excitation);
        sum += bp * qCached_[i] * gain_[i];
    }
    return sum;
}

void ModalResonator::updatePartialCoefficients()
{
    for (int i = 0; i < kMaxPartials; ++i)
        updatePartial(i);
}

void ModalResonator::updatePartial(int i)
{
    if (i < 0 || i >= kMaxPartials)
        return;

    // Inharmonicity stretching (piano-string formula).
    //   B = inharmonicity × 0.1  (so B ∈ [0, 0.1])
    //   stretched(i) = ratio(i) × sqrt(1 + B × ratio(i)²)
    const float r = ratio_[i];
    const float B = inharmonicity_ * 0.1f;
    const float stretched = r * std::sqrt(1.0f + B * r * r);
    const float freq = fundamental_ * stretched;

    // Nyquist guard: silently mute partials whose stretched frequency would
    // be above sr*0.45.  Held in reset so any feedback state is zeroed and
    // they contribute exactly 0 to the output sum.
    const float nyquistLimit = sampleRate_ * 0.45f;
    if (freq > nyquistLimit || freq < 10.0f)
    {
        alive_[i] = false;
        partial_[i].reset();
        qCached_[i] = kMinQ;
        return;
    }

    // Derive Q from decay so the BP's own impulse-response T60 equals the
    // requested decay seconds at this centre frequency:
    //   |pole|² ⋅ⁿ ≤ 10⁻³  ⟺  n × 2 ln|pole| ≤ −ln(1000)
    //   For a 2-pole resonator,  |pole| ≈ exp(−ω_b T / 2)  with  ω_b = ω/Q
    //   ⟹  Q = π × fc × decay / ln(1000)
    const float qRaw = (kPi * freq * decay_[i]) / kLn1k;
    qCached_[i]      = std::clamp(qRaw, kMinQ, kMaxQ);

    alive_[i] = true;
    partial_[i].setBandpass(freq, qCached_[i], sampleRate_);
}

} // namespace ideath
