#include <ideath/ReverbTank.h>
#include <algorithm>
#include <cmath>

namespace ideath {

static constexpr float kTwoPi = 6.283185307f;
static constexpr float kAntiDenormal = 1e-25f;

// Line lengths at 48 kHz (samples): mutually prime, 41.6–85.3 ms, sum
// 24 676 (0.514 s).  Total delay ≥ ~0.15 × T60 keeps the modal density
// high enough for a smooth tail at mid decays (Jot); line modulation
// smears the modes for the long ones.
static constexpr float kLineLengths48k[ReverbTank::kLines] = {
    1999.0f, 2333.0f, 2687.0f, 2903.0f, 3209.0f, 3571.0f, 3881.0f, 4093.0f
};

// Input diffuser: Dattorro's input-diffusion lengths (142, 107, 379, 277
// at 29.761 kHz) rescaled to 48 kHz, with his coefficients.
static constexpr int kDiffuserLengths48k[4] = { 229, 173, 611, 447 };
static constexpr float kDiffuserG[4] = { 0.75f, 0.75f, 0.625f, 0.625f };

// Unit-norm sign vectors (÷ √8 applied at use).  The input vector is not a
// Hadamard row and not the all-ones eigenvector of the Householder matrix,
// so every line receives the input and the mix spreads it at once.
static constexpr float kInVec[ReverbTank::kLines] = { 1, -1, 1, 1, -1, 1, -1, -1 };
static constexpr float kInvSqrtN = 0.35355339f; // 1 / √8

// Output tap: one unit-norm sign combination of all eight lines (a
// Hadamard row, ÷ √8), then two different allpass decorrelator chains for
// L and R.  |allpass| = 1, so L and R carry the same energy at every
// frequency — a held note cannot lean to one side.  (Two fixed taps, one
// per channel, give every mode a random L/R projection: a sustained low
// tone can lean up to 15 dB, and slowly rotating taps still leave ±10 dB.)  The chains' different dispersion decorrelates the diffuse
// tail: impulse-response correlation ≤ 0.12 in each of the < 200 Hz,
// 200 Hz–2 kHz and > 2 kHz bands.  Equal total length (900 samples at
// 48 kHz = 18.75 ms) gives both channels the same mean group delay, so
// neither side leads (no precedence-effect pull on transients).
static constexpr float kOutVec[ReverbTank::kLines] = { 1, 1, 1, 1, -1, -1, -1, -1 };
static constexpr int kDecorrLeft48k[4] = { 113, 167, 241, 379 };
static constexpr int kDecorrRight48k[4] = { 127, 181, 263, 329 };
static constexpr float kDecorrG = 0.65f; // + on the left chain, − on the right

// Input high-pass.  DC is a 7-fold degenerate tank mode (the Householder
// matrix has seven +1 eigenvalues and every delay has zero phase at DC),
// so infrasonic input piles up there at 1/(1 − g) — inaudible energy that
// eats headroom (measured 1.2 dB of a pink-noise tail below 20 Hz).
static constexpr float kInputHpHz = 20.0f;

// Second LFO runs at this ratio of the first so the eight line
// modulations never realign.
static constexpr float kLfo2Ratio = 1.3717f;

static int nextPow2(int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

// Damping low-pass: 2nd-order Butterworth, bilinear.  Its magnitude is
// |H|² = 1 / (1 + (Ω/Ω_c)⁴), Ω = tan(πf/sr): DC gain exactly 1 and a
// fourth-power roll-off, so the low end keeps the Jot decay.  (A one-pole
// low-pass reaching the same 4 kHz attenuation — up to 5 dB per pass —
// needs its corner near 2.7 kHz and shortens the 250 Hz decay by ≈ 20 %
// at T60 = 30 s; normalising it at 250 Hz instead would stretch the
// sub-250 Hz decay to ≈ 35 s.  The Butterworth's droop at 250 Hz is
// ≈ 0.0002 dB.)
static float butterMag2(float omegaC, float hz, float sampleRate)
{
    if (omegaC <= 0.0f)
        return 1.0f; // bypassed
    const float r = std::tan(3.14159265f * hz / sampleRate) / omegaC;
    return 1.0f / (1.0f + r * r * r * r);
}

void ReverbTank::DcBlocker::prepare(float cutoffHz, float sampleRate)
{
    a = std::exp(-kTwoPi * cutoffHz / sampleRate);
    reset();
}

float ReverbTank::DcBlocker::process(float x)
{
    const float y = a * (y1 + x - x1) + kAntiDenormal;
    x1 = x;
    y1 = y;
    return y;
}

ReverbTank::ReverbTank()
{
    prepare(44100.0f);
}

void ReverbTank::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    const float scale = sampleRate / 48000.0f;
    const int modHeadroom = static_cast<int>(std::ceil(kMaxModSec * sampleRate)) + 4;

    for (int i = 0; i < kLines; ++i)
    {
        Line& line = lines_[i];
        line.length = std::round(kLineLengths48k[i] * scale);
        const int size = nextPow2(static_cast<int>(line.length) + modHeadroom);
        line.buffer.assign(static_cast<size_t>(size), 0.0f);
        line.mask = size - 1;
    }
    auto setup = [scale](Diffuser& d, int length48k, float g) {
        d.size = std::max(1, static_cast<int>(std::round(static_cast<float>(length48k) * scale)));
        d.buffer.assign(static_cast<size_t>(d.size), 0.0f);
        d.g = g;
    };
    for (int k = 0; k < 4; ++k)
    {
        setup(diffusers_[static_cast<size_t>(k)], kDiffuserLengths48k[k], kDiffuserG[k]);
        setup(decorrLeft_[static_cast<size_t>(k)], kDecorrLeft48k[k], kDecorrG);
        setup(decorrRight_[static_cast<size_t>(k)], kDecorrRight48k[k], -kDecorrG);
    }

    // Input gain b: steady-state per-line output power is
    // p = b²·P_in / Σ(1 − g_i²) (energy injected per sample = energy lost
    // per sample, Householder mix lossless).  Each tap is a unit-norm sum
    // of decorrelated line outputs → tap power = p.  Unity at kUnityT60:
    // b² = Σ(1 − g_i²) evaluated there.
    float loss = 0.0f;
    for (const Line& line : lines_)
    {
        const float g = std::pow(10.0f, -3.0f * line.length / (sampleRate * kUnityT60));
        loss += 1.0f - g * g;
    }
    inputGain_ = std::sqrt(loss);
    inputHp_.prepare(kInputHpHz, sampleRate);
    inputHp2_.prepare(kInputHpHz, sampleRate);

    setModRate(modRate_);
    setModDepth(modDepth_);
    updateGains();
    reset();
}

void ReverbTank::reset()
{
    for (Line& line : lines_)
    {
        std::fill(line.buffer.begin(), line.buffer.end(), 0.0f);
        line.write = 0;
        line.z1 = 0.0f;
        line.z2 = 0.0f;
        line.apState = 0.0f;
    }
    for (auto* chain : { &diffusers_, &decorrLeft_, &decorrRight_ })
        for (Diffuser& d : *chain)
        {
            std::fill(d.buffer.begin(), d.buffer.end(), 0.0f);
            d.index = 0;
        }
    inputHp_.reset();
    inputHp2_.reset();
    lfoPhase_ = 0.0f;
    lfoPhase2_ = 0.25f;
    lfoOut_ = 0.0f;
    monoTap_ = 0.0f;
}

float ReverbTank::Diffuser::process(float x)
{
    // Schroeder allpass: w = x + g·w[n−N], y = w[n−N] − g·w.
    const float delayed = buffer[static_cast<size_t>(index)];
    const float w = x + g * delayed + kAntiDenormal;
    buffer[static_cast<size_t>(index)] = w;
    if (++index >= size)
        index = 0;
    return delayed - g * w;
}

void ReverbTank::setDecay(float t60Seconds)
{
    const float t = std::clamp(t60Seconds, kMinT60, kMaxT60);
    if (t == t60_)
        return;
    t60_ = t;
    updateGains();
}

void ReverbTank::setDamp(float damp)
{
    const float d = std::clamp(damp, 0.0f, 1.0f);
    if (d == damp_)
        return;
    damp_ = d;
    updateGains();
}

void ReverbTank::setModDepth(float depth)
{
    modDepth_ = std::clamp(depth, 0.0f, 1.0f);
    modSamples_ = modDepth_ * kMaxModSec * sampleRate_;
}

void ReverbTank::setModRate(float rateHz)
{
    modRate_ = std::clamp(rateHz, 0.05f, 5.0f);
    lfoInc_ = modRate_ / sampleRate_;
}

void ReverbTank::setFreeze(bool freeze)
{
    if (freeze == freeze_)
        return;
    freeze_ = freeze;
    updateGains();
}

void ReverbTank::updateGains()
{
    if (freeze_)
    {
        for (Line& line : lines_)
        {
            line.gain = 1.0f;
            line.setBypass();
            line.loss = 0.0f;
        }
        powerGain_ = INFINITY;
        pinkPowerGain_ = INFINITY;
        return;
    }

    // Jot: a line of L samples with gain g = 10^(−3·L / (sr·T60)) loses
    // 60 dB per T60 seconds whatever L is, so all lines decay together.
    // Damping adds kMaxDampDbPerSec·damp dB/s at kDampRefHz:
    //   T60_hf = 60 / (60/T60 + kMaxDampDbPerSec·damp).
    // The Butterworth low-pass supplies the ratio t = g_hf / g at the
    // reference: 1 / (1 + (Ω_ref/Ω_c)⁴) = t²  →  Ω_c = Ω_ref / (1/t² − 1)^¼.
    const float refHz = std::min(kDampRefHz, 0.45f * sampleRate_);
    const float omegaRef = std::tan(3.14159265f * refHz / sampleRate_);
    const float t60hf = 60.0f / (60.0f / t60_ + kMaxDampDbPerSec * damp_);

    float loss = 0.0f;
    for (Line& line : lines_)
    {
        const float g = std::pow(10.0f, -3.0f * line.length / (sampleRate_ * t60_));
        const float ghf = std::pow(10.0f, -3.0f * line.length / (sampleRate_ * t60hf));
        const float t = ghf / g;
        line.gain = g;
        if (t >= 0.9999f)
        {
            line.setBypass();
        }
        else
        {
            const float k = omegaRef / std::pow(1.0f / (t * t) - 1.0f, 0.25f);
            const float norm = 1.0f / (1.0f + 1.41421356f * k + k * k);
            line.omegaC = k;
            line.b0 = k * k * norm;
            line.b1 = 2.0f * line.b0;
            line.b2 = line.b0;
            line.a1 = 2.0f * (k * k - 1.0f) * norm;
            line.a2 = (1.0f - 1.41421356f * k + k * k) * norm;
        }
        line.loss = 1.0f - g * g;
        loss += line.loss;
    }
    powerGain_ = inputGain_ * inputGain_ / loss;

    // Pink weighting = equal weight per octave: mean over a third-octave
    // grid of G(f) = b² / Σ(1 − |H_i(f)|²), |H_i| = gain_i·|LP_i(f)| (the
    // allpass interpolator is unit-magnitude).
    float sum = 0.0f;
    int count = 0;
    for (float hz = 25.0f; hz <= std::min(16000.0f, 0.45f * sampleRate_); hz *= 1.2599210f)
    {
        float l = 0.0f;
        for (const Line& line : lines_)
            l += 1.0f - line.gain * line.gain * butterMag2(line.omegaC, hz, sampleRate_);
        sum += inputGain_ * inputGain_ / l;
        ++count;
    }
    pinkPowerGain_ = count > 0 ? sum / static_cast<float>(count) : powerGain_;
}

float ReverbTank::readLine(Line& line, float delay)
{
    // First-order allpass (Thiran) fractional delay: unit magnitude at every
    // frequency, so a modulated line loses no high end per pass (a Hermite
    // or linear read of a moving delay is a time-varying low-pass that
    // compounds over the tail — measured −4 dB at 16 kHz in steady state).
    // d = N + Δ with Δ ∈ [0.5, 1.5) keeps η = (1 − Δ)/(1 + Δ) in
    // (−0.2, 0.33], well away from the pole at −1.
    const int n = static_cast<int>(delay - 0.5f);
    const float frac = delay - static_cast<float>(n);
    const float eta = (1.0f - frac) / (1.0f + frac);
    const float xa = line.buffer[static_cast<size_t>((line.write - n) & line.mask)];
    const float xb = line.buffer[static_cast<size_t>((line.write - n - 1) & line.mask)];
    const float y = eta * xa + xb - eta * line.apState;
    line.apState = y;
    return y;
}

std::pair<float, float> ReverbTank::process(float input)
{
    // --- Input diffusion (Schroeder allpasses, lossless) ---
    float u = inputHp2_.process(inputHp_.process(input));
    for (Diffuser& d : diffusers_)
        u = d.process(u);
    u *= inputGain_ * kInvSqrtN;

    // --- LFOs: two quadrature pairs at incommensurate rates ---
    const float s1 = std::sin(kTwoPi * lfoPhase_);
    const float c1 = std::cos(kTwoPi * lfoPhase_);
    const float s2 = std::sin(kTwoPi * lfoPhase2_);
    const float c2 = std::cos(kTwoPi * lfoPhase2_);
    lfoPhase_ += lfoInc_;
    lfoPhase_ -= std::floor(lfoPhase_);
    lfoPhase2_ += lfoInc_ * kLfo2Ratio;
    lfoPhase2_ -= std::floor(lfoPhase2_);
    lfoOut_ = c2;
    const float mod[kLines] = { s1, c1, -s1, -c1, s2, c2, -s2, -c2 };

    // --- Read, absorb, mix ---
    float out[kLines];
    float damped[kLines];
    float sum = 0.0f;
    for (int i = 0; i < kLines; ++i)
    {
        Line& line = lines_[i];
        out[i] = readLine(line, line.length + mod[i] * modSamples_);
        // Damping low-pass (transposed direct form II).
        const float y = line.b0 * out[i] + line.z1;
        line.z1 = line.b1 * out[i] - line.a1 * y + line.z2 + kAntiDenormal;
        line.z2 = line.b2 * out[i] - line.a2 * y;
        damped[i] = line.gain * y;
        sum += damped[i];
    }

    // Householder: A = I − (2/N)·1·1ᵀ (orthogonal, O(N)).
    const float h = sum * (2.0f / static_cast<float>(kLines));
    float tap = 0.0f;
    for (int i = 0; i < kLines; ++i)
    {
        Line& line = lines_[i];
        line.buffer[static_cast<size_t>(line.write)] = damped[i] - h + u * kInVec[i];
        line.write = (line.write + 1) & line.mask;
        tap += kOutVec[i] * out[i];
    }
    monoTap_ = tap * kInvSqrtN;

    float left = monoTap_, right = monoTap_;
    for (Diffuser& d : decorrLeft_)
        left = d.process(left);
    for (Diffuser& d : decorrRight_)
        right = d.process(right);
    return { left, right };
}

} // namespace ideath
