#pragma once

#include "Voice.h"
#include <vector>

namespace ideath {

/// Polyphonic voice manager. Pre-allocates a pool of Voice instances
/// and handles note allocation, voice stealing, and mixing.
/// Real-time safe after prepare() — no allocation in process().
class Polyphony
{
public:
    static constexpr int kDefaultMaxVoices = 16;

    Polyphony() = default;

    /// Allocate voice pool and initialize all voices.
    void prepare(float sampleRate, int maxVoices = kDefaultMaxVoices);
    void reset();

    // --- Note control ---
    /// Trigger a note. Allocates an idle voice (or steals the oldest if full).
    void noteOn(float freqHz, float velocity = 1.0f);

    /// Release all voices matching this frequency.
    void noteOff(float freqHz);

    /// Release all active voices.
    void allNotesOff();

    /// Mix all active voices into a single sample.
    float process();

    // --- Voice configuration (applied to all voices) ---
    void setSource(Voice::Source src);
    void setOscWaveform(float wf);
    void setWavetable(const float* data, int size);
    void setWavetableNormalized(const float* data, int size);

    void setAttack(float seconds);
    void setDecay(float seconds);
    void setSustain(float level);
    void setRelease(float seconds);

    void setFilter(Voice::FilterType type, float freqHz, float q);

    void setLfoRate(float rateHz);
    void setLfoPitchDepth(float semitones);

    void setPortamento(float timeSec);

    void setBitDepth(int bits);
    void setDownsampleRate(float rateHz);

    // --- Query ---
    int getActiveVoiceCount() const;
    int getMaxVoices() const { return maxVoices_; }
    bool hasActiveVoices() const;

private:
    /// Find the best voice to steal: prefer idle, then oldest active.
    int findVoiceForNote() const;

    float sampleRate_ = 44100.0f;
    int maxVoices_ = kDefaultMaxVoices;
    std::vector<Voice> voices_;

    // Track allocation order for voice stealing (oldest first)
    std::vector<uint64_t> voiceAge_;
    uint64_t ageCounter_ = 0;
};

} // namespace ideath
