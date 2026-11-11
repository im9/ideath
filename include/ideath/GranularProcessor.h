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
/// `setSos` sits between the two extremes: new input is recorded over the
/// existing material instead of replacing it.
///
/// `setPlaybackDirection` launches grains that read backwards.  Grain length
/// is set by `setGrainSize` alone: speed and direction change how far a grain
/// travels, never how long it lasts.
///
/// API contract:
/// - `prepare(sr, bufferLengthSamples)` allocates the ring buffer.  This is
///   the only place that allocates — `process()`, `writeSample()`, and the
///   `set<Param>()` family are real-time safe (no allocation, no exceptions,
///   no locks).
/// - `process()` may be called before `prepare()` and will return 0.0f.
/// - Random pitch and position are driven by a deterministic xorshift32 RNG
///   seeded in `reset()` from `setSeed` (default 0x9E3779B9); two instances
///   with identical input, seed and parameter histories produce bit-identical
///   output.
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
    /// is positive), RNG re-seeded from the value given to `setSeed`.
    void reset();

    /// Push one sample into the ring buffer, mixed with what is already
    /// there according to `setSos`.  No-op if `freeze` or SOS 1 is on.
    void writeSample(float input);

    /// Grain spawn rate in Hz.  Clamped to `[0, sampleRate]`.  Zero disables
    /// spawning (any grains already in flight finish naturally and then the
    /// processor goes silent).
    void setGrainRate(float hz);

    /// Same as `setGrainRate`, but keeps the spawn phase: the fraction of the
    /// current grain period still to go is the same after the change, so a
    /// rate rise is heard at once instead of after the old period runs out.
    /// `setGrainRate` leaves the time to the next spawn as it was (raising
    /// 3.4 Hz → 40 Hz can wait up to ≈ 294 ms for the first faster grain);
    /// this one scales it by `oldRate / newRate`.
    ///
    /// Clamped like `setGrainRate`.  If the old or the new rate is 0 there is
    /// no period to keep a fraction of, and it does exactly what
    /// `setGrainRate` does.  Passing the current rate is a no-op, so a host can
    /// call it every sample.  Opt-in: callers that only use `setGrainRate`
    /// hear no change.  Near-zero rates (below ≈ 1e-38 Hz) would overflow
    /// the time to the next spawn; it is held at FLT_MAX, so the phase is
    /// lost there but a later rise spawns again within one new period.
    void setGrainRateKeepingPhase(float hz);

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
    /// or SOS 1 is on, the write head is stationary, so the window becomes
    /// effectively absolute within the frozen buffer.
    ///
    /// Default: `[0.0f, 0.5f]` — matches the previous `setPositionScatter(0.5f)`
    /// default so callers that don't touch this API see identical behaviour.
    void setReadWindow(float nearOffset01, float farOffset01);

    /// Freeze the write pointer.  When `true`, `writeSample()` becomes a
    /// no-op so the buffer contents stop changing — grain reads continue,
    /// turning the buffer into a static sample source for the cloud.
    void setFreeze(bool on);

    /// Sound-on-sound amount.  `writeSample()` stores
    /// `input * (1 - amount) + existing * amount`, so material fades by
    /// `amount` per pass of the write head and repeated passes drift away
    /// from the original recording.  Clamped to `[0, 1]`.
    ///
    /// 0 (default) overwrites, bit-identical to not calling this.  1 keeps the
    /// buffer exactly as it is and holds the write pointer, which makes it the
    /// same as `setFreeze(true)`: read windows stay put instead of sweeping
    /// through the buffer.  The write gain follows this value immediately, so
    /// glide it in the plugin layer if a large jump should not be recorded.
    void setSos(float amount01);

    /// Direction of newly launched grains; `true` reads backwards.  Grains
    /// already in flight keep the direction they were launched with, so a
    /// change never bends a sounding grain.  Default `false`.
    ///
    /// The ring holds its newest and oldest samples side by side at the write
    /// head.  A reverse grain runs into that seam from the oldest side, so it
    /// fades out through the seam and back in on the far side (2 ms each way
    /// at any speed, set from the head state at launch and capped at a quarter
    /// of the buffer).  The grain keeps its length and position while faded,
    /// so a very short reverse grain launched right at the head is quieter.
    void setPlaybackDirection(bool reverse);

    /// RNG seed, kept across `reset()` and `prepare()`.  A new value takes
    /// effect immediately; passing the current value is a no-op, so hosts can
    /// push it with the other parameters without restarting the sequence
    /// (`reset()` restarts it).  Bit-identical output needs the same seed;
    /// different seeds give different grains unless nothing is random (zero
    /// pitch spread and a degenerate read window).  xorshift32 cannot leave
    /// state 0, so seed 0 runs from `kZeroSeedState` instead, the only pair of
    /// seeds that share a sequence.  Default 0x9E3779B9.
    void setSeed(uint32_t seed);

    /// Process one sample, returning the gain-compensated sum of all active
    /// grains.  Spawns new grains internally based on the spawn timer.
    float process();

    /// Number of grains currently active.  Exposed for tests and metering.
    int activeGrainCount() const;

    /// Per-grain state exposed for visualisation.  `readPos` is a float index
    /// into the ring buffer (0..bufferSize) — sub-sample because grains read
    /// with linear interpolation.  `envPhase` is the grain's normalised
    /// lifetime position in `[0, 1)`.  `pitch` is the varispeed playback
    /// ratio (2^(semis/12)), positive in both directions.  Inactive slots
    /// have `active=false` and the other fields are undefined.
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
    /// (`setFreeze(true)` or `setSos(1)`), this value stops advancing.
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
        float step     = 1.0f;   // signed readPos increment, negative = reverse
        float envPhase = 0.0f;   // 0..1 over the grain's lifetime
        float envInc   = 0.0f;   // 1 / grainSizeSamples (frozen at spawn)
        float seamFadeInv = 1.0f; // 1 / seam fade length in samples (reverse only)
    };

    void  trySpawnGrain();
    float renderGrain(Grain& g) const;
    float reverseSeamGain(const Grain& g) const;
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
    float sos_             = 0.0f;      // 0 = overwrite, 1 = keep existing
    bool  reverse_         = false;     // direction of newly spawned grains

    // Spawn timer.  Counts down by 1/sampleRate per call; when it reaches 0
    // (or below) a grain is launched and 1/grainRate is added to it.
    // setGrainRateKeepingPhase rescales it so timer · grainRate (the part of
    // the period still to go) holds across a rate change.
    float spawnTimer_ = 0.0f;

    // Deterministic RNG (xorshift32, matching ideath::Noise).
    uint32_t rngState_ = 0x9E3779B9u;
    static constexpr uint32_t kRngSeed = 0x9E3779B9u;
    // Mersenne Twister's init multiplier; any non-zero value other than the
    // default seed works.
    static constexpr uint32_t kZeroSeedState = 0x6C078965u;
    uint32_t seed_ = kRngSeed;
};

} // namespace ideath
