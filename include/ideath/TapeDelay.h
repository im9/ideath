#pragma once

#include "AllpassFilter.h"
#include "Biquad.h"
#include <array>
#include <utility>
#include <vector>

namespace ideath {

/// Tape-style delay with wow/flutter modulation, feedback tone shaping,
/// saturated repeats, in-loop diffusion and an optional stereo output.
///
/// `process()` is the mono delay.  `processStereo()` runs the same delay
/// with a stereo output (Offset: the right channel reads the loop
/// `stereoOffset` later; PingPong: two cross-fed loops, echoes alternate
/// L → R).  Use one or the other on an instance, not both.
///
/// Reads use 4-point cubic Hermite interpolation (repeats stay bright at
/// fractional delays, and moving reads stay smooth), so the shortest delay
/// is 2 samples.
///
/// Output ceiling for ±1 input: ±1 (saturation on: tanh; saturation off:
/// transparent below 0.8, soft ceiling at 1).
class TapeDelay
{
public:
    enum class StereoMode { Offset, PingPong };

    TapeDelay() { prepare(44100.0f); }

    void prepare(float sampleRate, float maxDelaySec = 2.0f);
    void reset();

    /// Base delay time in seconds.
    void setDelay(float delaySec);

    /// Feedback amount. Clamped to [-0.999, 0.999].
    void setFeedback(float feedback);

    /// Dry/wet mix (0 = fully dry, 1 = fully wet).
    void setMix(float mix);

    /// Wow modulation depth/rate (slow, larger drift).
    void setWowDepth(float seconds);
    void setWowRate(float rateHz);

    /// Flutter modulation depth/rate (fast, small wobble).
    void setFlutterDepth(float seconds);
    void setFlutterRate(float rateHz);

    /// Feedback path tone shaping.
    void setLowpass(float freqHz);
    void setHighpass(float freqHz);

    /// Saturation drive on repeats (>= 1.0).
    void setDrive(float drive);

    /// Saturation on repeats.  true (default): tanh(x·drive).  false: clean
    /// digital repeats — transparent below |x| = 0.8, soft ceiling at 1.
    void setSaturation(bool enabled);

    /// In-loop diffusion (0–1).  Four Schroeder allpasses (≈ 10 ms in all)
    /// smear every repeat a little more, so the echoes blur into a wash.
    /// Their length is taken out of the loop read, so each repeat's energy
    /// centroid stays at the echo time (the smear spreads around it); at 0
    /// the repeats are exactly as without diffusion.  When the delay minus
    /// the wow/flutter depth is under ≈ 10.5 ms the undiffused repeats are
    /// read instead (5 ms crossfade, no click).  At 0 the diffuser does not
    /// run at all; changes glide over ≈ 10 ms.
    void setDiffusion(float amount);

    /// Stereo output shape for processStereo().  Default Offset.
    void setStereoMode(StereoMode mode);

    /// Offset mode: right channel reads this much later (0–0.05 s).
    void setStereoOffset(float seconds);

    float process(float input);

    /// Mono in, stereo out.  Wow/flutter run in quadrature on the right
    /// channel so the modulation also widens the image.
    std::pair<float, float> processStereo(float input);

    float getDelaySamples() const { return delaySamples_; }

private:
    struct Loop
    {
        std::vector<float> buffer;   // repeats as written
        std::vector<float> diffused; // the same, through the diffuser
        int writePos = 0;
        Biquad highpass;
        Biquad lowpass;
        std::array<AllpassFilter, 4> diffuser;
    };

    float readDelay(const std::vector<float>& buffer, int writePos, float delaySamples) const;
    void updateFeedbackFilters();
    float color(Loop& loop, float wet);
    float loopRead(const Loop& loop, float delay) const;
    bool diffuserActive() const;
    void clearLoop(Loop& loop);
    void applyDiffuseG();
    void startDiffuser();
    std::pair<float, float> run(float input, bool stereo);
    void writeLoop(Loop& loop, float value);
    void advanceLfos(float& wow, float& flutter, float* wowQ, float* flutterQ);

    float sampleRate_ = 44100.0f;
    Loop a_; // mono loop / left
    Loop b_; // ping-pong right loop
    Biquad offsetHighpass_;
    Biquad offsetLowpass_;
    int bufferSize_ = 0;
    int bufferMask_ = 0;
    float maxDelaySamples_ = 0.0f;

    bool saturation_ = true;
    float diffusion_ = 0.0f;
    float diffuserSamples_ = 0.0f;
    StereoMode stereoMode_ = StereoMode::Offset;
    float offsetSamples_ = 0.0f;
    float offsetSec_ = 0.0f;
    float diffuseMix_ = 0.0f; // 0 = straight read, 1 = diffused read
    bool chainOn_ = false;    // diffuser running, diffused copy written
    int diffusedFill_ = 0;    // samples written to the diffused copy (capped)
    int written_ = 0;         // samples written since reset (capped)
    float gTarget_ = 0.0f;    // allpass g asked for
    float gNow_ = 0.0f;       // allpass g in use (glides to gTarget_)
    float diffuseGlide_ = 0.0f;

    float delaySamples_ = 1.0f;
    float feedback_ = 0.0f;
    float mix_ = 1.0f;

    // LFO as a unit phasor: re = cos, im = sin of the phase.
    struct Phasor
    {
        float re = 1.0f;
        float im = 0.0f;
    };
    static void rotate(Phasor& p, const Phasor& step);

    float wowDepthSamples_ = 0.0f;
    Phasor wow_;
    Phasor wowStep_;

    float flutterDepthSamples_ = 0.0f;
    Phasor flutter_;
    Phasor flutterStep_;

    float lowpassHz_ = 8000.0f;
    float highpassHz_ = 40.0f;
    float drive_ = 1.5f;

};

} // namespace ideath
