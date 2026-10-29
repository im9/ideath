#pragma once

#include "ReverbTank.h"
#include <utility>
#include <vector>

namespace ideath {

/// Hall reverb: pre-delay → ReverbTank (8-line Householder FDN with input
/// diffusion, modulated lines, Jot absorption).  Shares the tank with
/// ShimmerReverb, so switching between the two keeps the tail's texture.
///
/// Mono in, stereo out.
///
/// Output level: steady-state broadband RMS gain √(T60 / 3.87 s) — 0 dB at
/// size 0.5, +8.9 dB at size 1, −8.9 dB at size 0.
/// Output ceiling for ±1 input: ±kMaxOutput (35).  Derivation: a steady
/// sine parked on a tank mode gains (peak / mean modal power) =
/// 2 / (ΣL·δ) = 2·T60 / (6.908 · ΣL_sec) = 16.9 at T60 = 30 s
/// (ΣL_sec = 0.514 s), times the broadband power gain 30 / 3.87 = 7.75,
/// → amplitude √131 = 11.4.  Modal amplitudes are Rayleigh-distributed, so
/// the strongest of the ≈ 10⁴ modes below 20 kHz carries ln(10⁴) ≈ 9.2×
/// the mean power (×3.03 amplitude) → 34.6.  Modulation (default depth
/// 0.5) smears modes and keeps program material far below this.
/// Place a PeakLimiter downstream.
class HallReverb
{
public:
    static constexpr float kMaxOutput = 35.0f;
    static constexpr float kMaxPreDelay = 0.12f; // seconds

    HallReverb();

    void prepare(float sampleRate);
    void reset();

    /// Room size (0.0–1.0).  Low-frequency T60 = 0.5 s · 60^size
    /// (0.5 s → 30 s, even ratio per step of size).
    void setSize(float size);

    /// Damping (0.0–1.0).  High-frequency absorption: extra decay at 4 kHz
    /// rises linearly from 0 to 60 dB/s.  Low values = bright, open.
    void setDamp(float damp);

    /// Pre-delay time in seconds (0.0–0.12).
    void setPreDelay(float seconds);

    /// Line modulation depth (0.0–1.0 → 0–1 ms excursion).  Chorus-like
    /// movement that smears the tank's modes.
    void setModDepth(float depth);

    /// Line modulation rate in Hz (0.05–5).
    void setModRate(float rateHz);

    /// Dry/wet mix (0.0 = fully dry, 1.0 = fully wet).
    void setMix(float mix);

    /// Freeze mode.  The tank becomes lossless and input is muted, so the
    /// tail is held indefinitely.
    void setFreeze(bool freeze);

    /// Process one mono input sample.  Returns {left, right}.
    std::pair<float, float> process(float input);

    /// T60 (seconds) that setSize(size) asks of the tank.
    static float sizeToT60(float size);

private:
    float sampleRate_ = 44100.0f;
    float size_ = 0.7f;
    float mix_ = 0.5f;
    bool freeze_ = false;

    std::vector<float> preDelay_;
    int preDelayMask_ = 0;
    int preDelayWrite_ = 0;
    int preDelaySamples_ = 0;
    float preDelaySec_ = 0.0f;

    ReverbTank tank_;
};

} // namespace ideath
