#pragma once

#include <array>

namespace ideath {

/// Multi-shape band-limited wavetable oscillator (general-purpose synthesis).
///
/// Holds a fixed palette of 10 wave shapes (sine/square/saw/tri/pulse/superSaw/
/// metallic/spectral/formantA/formantO).  Each shape is pre-baked into a 2048-sample
/// table with 9 mipmap levels indexed by playback frequency, giving alias-free output
/// from ~10 Hz to Nyquist on any common sample rate.
///
/// Use this when you want a "modern" wavetable engine (inboil / Plaits / Serum-class):
/// rich shape palette, anti-aliased, morph-friendly.  Use `Wavetable` instead when
/// you want raw 4-bit Game Boy stairsteps with no band-limiting — they are
/// intentionally separate primitives.
///
/// Shape morphing: `setShapePosition(p)` with `p in [0, shapeCount-1]` linearly
/// interpolates between adjacent shapes in enum order.  E.g. p=2.5 morphs Saw↔Triangle.
class MultiShapeWavetable
{
public:
    static constexpr int kTableSize = 2048;
    static constexpr int kMipmapLevels = 9;

    /// Enum order defines morph order — adjacent enum entries are adjacent on the
    /// morph slider.  Order is "tame → wild" so a position sweep crosses recognisable
    /// territory first (Sine → Square → Saw → Triangle → Pulse) before exotic shapes
    /// (SuperSaw → Metallic → Spectral → FormantA → FormantO).
    enum class Shape
    {
        Sine = 0,
        Square,
        Saw,
        Triangle,
        Pulse,
        SuperSaw,
        Metallic,
        Spectral,
        FormantA,
        FormantO,
        Count
    };

    MultiShapeWavetable() = default;

    /// Build (or look up cached) 10-shape × 9-mipmap table set for this sample rate.
    /// Table generation costs ~10 ms; cached statically across all instances at the
    /// same `sampleRate`.  Subsequent `process()` calls are real-time safe.
    void prepare(float sampleRate);
    void reset();

    void setFrequency(float freqHz);

    /// Snap to a single shape (equivalent to setShapePosition((float)s)).
    void setShape(Shape s);

    /// Continuous morph position in `[0, shapeCount() - 1]`.  Fractional values
    /// crossfade linearly between adjacent shapes.  Clamped on entry.
    void setShapePosition(float pos);

    float process();

    float getPhase() const { return phase_; }
    float getFrequency() const { return freqHz_; }
    float getShapePosition() const { return shapePos_; }

    static constexpr int shapeCount() { return static_cast<int>(Shape::Count); }

    /// Mipmap level selection for a playback frequency (exposed for testing).
    /// Boundaries: [64, 128, 256, 512, 1024, 2048, 4096, 8192] Hz.
    static int mipmapLevelFor(float freqHz);

    /// Opaque shared-table storage type.  Public so the cpp's cache helpers
    /// can reference it; instances never construct one directly.
    struct TableSet;

private:
    static constexpr int kShapeCount = static_cast<int>(Shape::Count);

    /// Lazy-init shared cache keyed by sample rate.  Table sets are immutable after
    /// construction; multiple instances share the same const pointer.  Mutex-guarded
    /// at init time only; `process()` reads the pointer with no synchronisation.
    static const TableSet& getOrBuildTables(float sampleRate);

    float sampleRate_ = 44100.0f;
    float phase_ = 0.0f;
    float phaseInc_ = 0.0f;
    float freqHz_ = 0.0f;
    float shapePos_ = 0.0f;
    const TableSet* tables_ = nullptr;
};

} // namespace ideath
