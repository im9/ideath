#pragma once

#include <ideath/LowPassGate.h>
#include <ideath/Oscillator.h>

namespace ideath {

/// LowPassGate + carrier oscillator bundle for the slothrop "Ping" engine.
///
/// Wraps a morphing saw↔square oscillator (the same `Oscillator` primitive
/// used by every other ideath voice) through a `LowPassGate`.  The
/// slothrop 3-knob Ping config maps directly:
///
///     Tone       → setTone()        — 0 = pure square, 1 = pure saw
///     Damping    → setDamping()     — passes through to LowPassGate
///     Brightness → setBrightness()  — passes through to LowPassGate
///
/// `ping(velocity)` fires the vactrol envelope; the carrier runs
/// continuously (no carrier gate — the LPG envelope IS the gate).
///
/// This bundle exists because the canonical Ping use case is "carrier
/// through LPG" — duplicating the Oscillator + LowPassGate wiring at
/// every call site (REPL, slothrop Engine case, plugin layer) would be
/// boilerplate.  When other carrier sources are needed (sine / tri /
/// wavetable / FM), use the bare `LowPassGate::process(carrier)` API.
///
/// Output bound is identical to `LowPassGate`: ±1.3 (LP DC gain × envelope
/// × ±1 carrier).
class LowPassGateVoice
{
public:
    LowPassGateVoice() = default;

    void prepare(float sampleRate);
    void reset();

    /// Carrier pitch (Hz).  Forwarded to the internal Oscillator with its
    /// own clamping.
    void setFrequency(float hz);

    /// Carrier waveform morph in `[0, 1]`.  0 = pure square, 1 = pure saw.
    /// Matches `Oscillator::process(morph)` semantics so the existing
    /// REPL / Voice convention carries over.
    void setTone(float t);

    /// LowPassGate fall-time scaler in `[0, 1]`.  See `LowPassGate::setDamping`.
    void setDamping(float d);

    /// LowPassGate peak cutoff in `[0, 1]`.  See `LowPassGate::setBrightness`.
    void setBrightness(float b);

    /// Fire the vactrol envelope on the LPG.  `velocity` in `[0, 1]`.
    void ping(float velocity = 1.0f);

    /// Produce one output sample (carrier → LPG).
    float process();

private:
    Oscillator  carrier_;
    LowPassGate lpg_;
    float       tone_ = 1.0f;  // saw by default
};

} // namespace ideath
