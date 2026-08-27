#pragma once

#include <cstdint>
#include <vector>

namespace ideath {

/// Granular processor — ring buffer grain cloud.
///
/// `writeSample()` pushes incoming audio into a circular buffer.  An internal
/// spawn-timer periodically launches Hann-windowed grains that read from
/// pseudo-random positions inside a configurable read window behind the
/// current write head (see `setReadWindow` for the window definition; the
/// legacy `setPositionScatter` is a single-parameter convenience for the
/// window `[0, amount]`).  Each grain has an independent pitch ratio drawn
/// from `pitchOffset + [-pitchSpread, +pitchSpread]` semitones, played back
/// via linear-interpolated varispeed read.  `pitchOffset` is the bipolar
/// centre (default 0 = unity) and `pitchSpread` is the random deviation
/// around it.  Outputs are summed and gain-compensated for the expected
/// overlap (`grainRate * grainSize` grains in flight).
///
/// When `freeze` is enabled `writeSample()` becomes a no-op (the write
/// pointer holds its position), so the buffer becomes a static sample source
/// while existing grains continue to scrub through the captured material.
///
/// API contract:
/// - `prepare(sr, bufferLengthSamples)` allocates the ring buffer.  This is
///   the only place that allocates — `process()`, `writeSample()`, and the
///   `set<Param>()` family are real-time safe (no allocation, no exceptions,
///   no locks).
/// - `process()` may be called before `prepare()` and will return 0.0f.
/// - Random pitch and position are driven by a deterministic xorshift32 RNG
///   seeded in `reset()`; two instances with identical input and identical
///   parameter histories produce bit-identical output.
///
/// Visualisation hooks (`snapshotGrains`, `writePos`, `bufferSize`) expose
/// the current grain-pool state and ring-buffer geometry so that UIs can
/// draw the actual live grains inside the tape read window rather than a
/// procedural preview keyed off knob values (which would drift out of sync
/// with the audio due to independent RNG draws).
class GranularProcessor
{
public:
    /// Maximum number of simultaneously active grains.  Chosen at 16 to give
    /// comfortable headroom over the typical 4–8 overlap implied by mid
    /// `grainRate * grainSize`; extra spawn requests when the pool is full
    /// are dropped (no allocation, no replacement) — see Cloud-style grain
    /// engines (Mutable Instruments Clouds, ML Granulator) which cap grain
    /// count at 16–40 for the same reason.
    static constexpr int kMaxGrains = 16;

    GranularProcessor() = default;

    /// Allocate the ring buffer (`bufferLengthSamples` samples) and reset
    /// internal state.  Buffer length is clamped to at least 16 samples to
    /// keep the modulo arithmetic well-defined.
    void prepare(float sampleRate, int bufferLengthSamples);

    /// Return to initial state: buffer cleared, all grains inactive, spawn
    /// timer zeroed (next `process()` will spawn immediately if `grainRate`
    /// is positive), RNG re-seeded.
    void reset();

    /// Push one sample into the ring buffer.  No-op if `freeze` is on.
    void writeSample(float input);

    /// Grain spawn rate in Hz.  Clamped to `[0, sampleRate]`.  Zero disables
    /// spawning (any grains already in flight finish naturally and then the
    /// processor goes silent).
    void setGrainRate(float hz);

    /// Grain length in seconds.  Clamped to `[1e-4, 1.0]` (0.1 ms → 1 s).
    void setGrainSize(float seconds);

    /// Pitch randomisation amount in semitones.  Each grain is launched at a
    /// random pitch in `[-pitchSpread, +pitchSpread]` around `pitchOffset`.
    /// Clamped to `[0, 24]` (two octaves either side).
    void setPitchSpread(float semitones);

    /// Global pitch centre in semitones (bipolar transposition).  Every
    /// grain is launched at `pitchOffset + uniform(-pitchSpread, +pitchSpread)`
    /// semitones, so this shifts the spread window without introducing
    /// randomness.  Default 0 = unity.  Clamped to `[-24, +24]` (two octaves
    /// either side), matching `setPitchSpread`.
    void setPitchOffset(float semitones);

    /// Convenience wrapper for `setReadWindow(0.0f, amount)`.  Preserves the
    /// pre-`setReadWindow` API: every grain starts reading somewhere in the
    /// range `[0, amount * bufferSize]` samples behind the write head.
    /// `amount` is clamped to `[0, 1]`.
    void setPositionScatter(float amount);

    /// Read window bounds, expressed as fractional offsets BEHIND the
    /// current write head.  Grains launch at a random position uniformly
    /// drawn from `[nearOffset * bufferSize, farOffset * bufferSize]`
    /// samples behind the write head.  Both operands are clamped to
    /// `[0, 1]`; if `nearOffset > farOffset` after clamping the two are
    /// swapped so the window is always stored in canonical `[min, max]`
    /// order.
    ///
    /// A degenerate window (`nearOffset == farOffset`) pins every grain to
    /// the same relative offset (zero position randomness).  When `freeze`
    /// is on, the write head is stationary, so the window becomes
    /// effectively absolute within the frozen buffer.
    ///
    /// Default: `[0.0f, 0.5f]` — matches the previous `setPositionScatter(0.5f)`
    /// default so callers that don't touch this API see identical behaviour.
    void setReadWindow(float nearOffset01, float farOffset01);

    /// Freeze the write pointer.  When `true`, `writeSample()` becomes a
    /// no-op so the buffer contents stop changing — grain reads continue,
    /// turning the buffer into a static sample source for the cloud.
    void setFreeze(bool on);

    /// Process one sample, returning the gain-compensated sum of all active
    /// grains.  Spawns new grains internally based on the spawn timer.
    float process();

    /// Number of grains currently active.  Exposed for tests and metering.
    int activeGrainCount() const;

    /// Per-grain state exposed for visualisation.  `readPos` is a float index
    /// into the ring buffer (0..bufferSize) — sub-sample because grains read
    /// with linear interpolation.  `envPhase` is the grain's normalised
    /// lifetime position in `[0, 1)`.  `pitch` is the varispeed playback
    /// ratio (2^(semis/12)).  Inactive slots have `active=false` and the
    /// other fields are undefined.
    struct GrainSnapshot
    {
        bool  active;
        float readPos;
        float envPhase;
        float pitch;
    };

    /// Copy the current grain-pool state into `out`.  Writes exactly
    /// `kMaxGrains` entries — caller sizes the buffer to `kMaxGrains` and
    /// filters by `active`.  Non-blocking; snapshot may see torn reads on
    /// individual grain fields since the audio thread advances them per
    /// sample, but each grain is small enough that a torn read affects only
    /// a single frame of visual and averages out at UI framerate.
    void snapshotGrains(GrainSnapshot* out) const;

    /// Current ring-buffer write index in samples.  Combined with `readPos`
    /// from `snapshotGrains` this lets a UI compute `offsetBehindHead =
    /// ((writePos - readPos) mod bufferSize) / bufferSize` for a cloud
    /// visualisation whose right edge tracks the write head.  When frozen
    /// (`setFreeze(true)`), this value stops advancing.
    int writePos() const;

    /// Ring-buffer length in samples (as passed to `prepare`, subject to the
    /// 16-sample floor documented there).  Zero before `prepare` is called.
    int bufferSize() const;

private:
    struct Grain
    {
        bool  active   = false;
        float readPos  = 0.0f;   // float index into ring buffer
        float pitch    = 1.0f;   // playback rate (1.0 = unity)
        float envPhase = 0.0f;   // 0..1 over the grain's lifetime
        float envInc   = 0.0f;   // 1 / grainSizeSamples (frozen at spawn)
    };

    void  trySpawnGrain();
    float renderGrain(Grain& g) const;
    float nextUniform01();              // xorshift32 in [0, 1)
    float nextUniformPM1();             // xorshift32 in [-1, +1)

    float sampleRate_    = 44100.0f;
    std::vector<float> buffer_;
    int   bufferSize_    = 0;
    int   writePos_      = 0;

    Grain grains_[kMaxGrains] {};

    // Parameters (raw, post-clamp).
    float grainRate_       = 20.0f;     // Hz
    float grainSize_       = 0.1f;      // seconds
    float pitchSpread_     = 0.0f;      // semitones (±)
    float pitchOffset_     = 0.0f;      // semitones (centre of spread window)
    // Read window [near, far] in fractional buffer offsets behind writePos_.
    // Default [0.0, 0.5] reproduces the pre-setReadWindow default of
    // setPositionScatter(0.5).
    float readWindowNear_  = 0.0f;
    float readWindowFar_   = 0.5f;
    bool  frozen_          = false;

    // Spawn timer.  Counts down by 1/sampleRate per call; when it reaches 0
    // (or below) a grain is launched and the timer is reset to 1/grainRate.
    float spawnTimer_ = 0.0f;

    // Deterministic RNG (xorshift32, matching ideath::Noise).
    uint32_t rngState_ = 0x9E3779B9u;
    static constexpr uint32_t kRngSeed = 0x9E3779B9u;
};

} // namespace ideath
