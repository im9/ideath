#include <ideath/MultiShapeWavetable.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ideath {

namespace {

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

// Octave-band mipmap boundary frequencies — mirrors inboil melodic.ts L637.
// Level k's table is generated using band-limit baseFreq = kMipmapFreqs[k],
// so it contains every harmonic from 1 up to floor(nyquist / baseFreq).
// At playback freq f, we select the SMALLEST k whose baseFreq > f, capped at
// the last level.  This guarantees no harmonic of the playback freq exceeds
// Nyquist.
constexpr float kMipmapFreqs[MultiShapeWavetable::kMipmapLevels] = {
    32.0f, 64.0f, 128.0f, 256.0f, 512.0f, 1024.0f, 2048.0f, 4096.0f, 8192.0f
};

constexpr int kTableSize = MultiShapeWavetable::kTableSize;
constexpr int kMipmapLevels = MultiShapeWavetable::kMipmapLevels;
constexpr int kShapeCount = MultiShapeWavetable::shapeCount();

using Shape = MultiShapeWavetable::Shape;

// --- Shape generators (additive synthesis, then peak-normalise) ----------

// Per-harmonic amplitude for the simple additive shapes.  Returns 0 for
// silent harmonics.  Inboil parity, melodic.ts L446-461.
float harmonicAmp(Shape s, int h)
{
    switch (s)
    {
        case Shape::Sine:
            return (h == 1) ? 1.0f : 0.0f;
        case Shape::Saw:
            // Alternating-sign 1/h (Fourier series of sawtooth).
            return ((h % 2 == 0) ? -1.0f : 1.0f) * (1.0f / static_cast<float>(h));
        case Shape::Square:
            return (h % 2 == 1) ? (1.0f / static_cast<float>(h)) : 0.0f;
        case Shape::Triangle:
            // Triangle: odd harmonics with 1/h² rolloff, alternating sign
            // pattern h%4==1 → +, h%4==3 → −.
            return (h % 2 == 1)
                ? ((h % 4 == 1) ? 1.0f : -1.0f) / static_cast<float>(h * h)
                : 0.0f;
        case Shape::Pulse:
            // Placeholder — actual amplitude is sin(πhw)/(πh/2) with w = duty
            // cycle, computed in buildPulse below (needs a fundamental cosine
            // basis instead of sine, so it can't share the additive sine path).
            // Returning a non-zero amp here would double-count.  We return 0 and
            // dispatch to buildPulse in generateTable.
            return 0.0f;
        case Shape::Spectral:
            // Inboil "spectral": odd harmonics only with 1/(h²·0.5) = 2/h² rolloff.
            // Steeper than triangle in absolute terms; same shape post-normalisation.
            return (h % 2 == 1)
                ? (1.0f / (static_cast<float>(h * h) * 0.5f))
                : 0.0f;
        default:
            return 0.0f;
    }
}

void normalise(float* table, int size)
{
    float peak = 0.0f;
    for (int i = 0; i < size; ++i)
    {
        const float a = std::abs(table[i]);
        if (a > peak) peak = a;
    }
    if (peak > 1e-12f)
    {
        const float inv = 1.0f / peak;
        for (int i = 0; i < size; ++i) table[i] *= inv;
    }
}

void zero(float* table, int size)
{
    for (int i = 0; i < size; ++i) table[i] = 0.0f;
}

void addAdditive(float* table, int size, Shape shape, int maxHarmonics)
{
    for (int h = 1; h <= maxHarmonics; ++h)
    {
        const float amp = harmonicAmp(shape, h);
        if (amp == 0.0f) continue;
        for (int i = 0; i < size; ++i)
        {
            const float angle = kTwoPi * static_cast<float>(h) * static_cast<float>(i)
                              / static_cast<float>(size);
            table[i] += amp * std::sin(angle);
        }
    }
}

void buildPulse(float* table, int size, int maxHarmonics)
{
    // Band-limited narrow pulse, 5% duty cycle.  Analytical Fourier series
    // for a rectangular pulse of width w (centred at t=0), DC-removed:
    //   f(t) = sum_{h=1..N} (2 sin(πhw) / (πh)) · cos(2π h t / T)
    // The cosine basis differs from the additive sine shapes (Saw, Square,
    // Sine, Triangle, Spectral) so this shape gets its own builder.
    //
    // Spectral signature: amp envelope is sin(πhw)/(πh/2), near-flat at amp
    // ≈ 2w ≈ 0.1 for h < 1/w = 20, then nulls at every h·w = integer.
    // After peak normalisation the time-domain peak ≈ (1 - w) lifts all bins
    // by ~10% — this is what produces audible, harmonically rich pulse tone
    // distinct from Saw's 1/h envelope.
    constexpr float kDuty = 0.05f;
    for (int h = 1; h <= maxHarmonics; ++h)
    {
        const float amp = std::sin(kPi * static_cast<float>(h) * kDuty)
                        / (kPi * static_cast<float>(h) * 0.5f);
        for (int i = 0; i < size; ++i)
        {
            const float angle = kTwoPi * static_cast<float>(h) * static_cast<float>(i)
                              / static_cast<float>(size);
            table[i] += amp * std::cos(angle);
        }
    }
}

void buildSuperSaw(float* table, int size, int maxHarmonics)
{
    // Inboil L471-484: 7 detuned saws summed with phase offsets per harmonic.
    // The factor 1/7 normalises the sum back to single-saw amplitude before
    // the global peak normalisation, keeping each detune voice's contribution
    // balanced.
    static constexpr float detunes[7] = { -0.12f, -0.08f, -0.04f, 0.0f, 0.04f, 0.08f, 0.12f };
    for (float dt : detunes)
    {
        for (int h = 1; h <= maxHarmonics; ++h)
        {
            const float amp = ((h % 2 == 0) ? -1.0f : 1.0f)
                            * (1.0f / static_cast<float>(h)) / 7.0f;
            const float phaseOff = dt * static_cast<float>(h) * 2.5f;
            for (int i = 0; i < size; ++i)
            {
                const float angle = kTwoPi * static_cast<float>(h) * static_cast<float>(i)
                                  / static_cast<float>(size);
                table[i] += amp * std::sin(angle + phaseOff);
            }
        }
    }
}

void buildMetallic(float* table, int size, float baseFreq, float sr)
{
    // Inboil L528-540: inharmonic partials at non-integer multiples of the
    // fundamental.  We band-limit by skipping any ratio whose absolute
    // playback frequency (ratio * baseFreq) would exceed Nyquist.
    static constexpr float ratios[6] = { 1.0f, 1.47f, 2.09f, 2.56f, 3.14f, 4.2f };
    static constexpr float amps[6]   = { 1.0f, 0.7f,  0.5f,  0.35f, 0.25f, 0.15f };

    const float nyquist = sr * 0.5f;
    for (int r = 0; r < 6; ++r)
    {
        if (ratios[r] * baseFreq > nyquist) continue;
        for (int i = 0; i < size; ++i)
        {
            const float angle = kTwoPi * ratios[r] * static_cast<float>(i)
                              / static_cast<float>(size);
            table[i] += amps[r] * std::sin(angle);
        }
    }
}

void buildFormant(float* table, int size, float baseFreq, float sr,
                  float f1, float bw1, float a1Amp,
                  float f2, float bw2, float a2Amp,
                  float f3, float bw3, float a3Amp,
                  int maxHarmonics)
{
    // Inboil L485-516: each harmonic's amplitude is the sum of three Gaussian
    // peaks centred at the formant frequencies, with an additional 1/h
    // envelope so high harmonics don't dominate.  Skips harmonics whose
    // combined amplitude falls below 0.001 to avoid wasted compute.
    (void)sr; // f, baseFreq, nyquist already implicit in maxHarmonics
    for (int h = 1; h <= maxHarmonics; ++h)
    {
        const float f = static_cast<float>(h) * baseFreq;
        const float g1 = std::exp(-0.5f * ((f - f1) / bw1) * ((f - f1) / bw1)) * a1Amp;
        const float g2 = std::exp(-0.5f * ((f - f2) / bw2) * ((f - f2) / bw2)) * a2Amp;
        const float g3 = std::exp(-0.5f * ((f - f3) / bw3) * ((f - f3) / bw3)) * a3Amp;
        const float amp = (g1 + g2 + g3) / static_cast<float>(h);
        if (std::abs(amp) < 0.001f) continue;
        for (int i = 0; i < size; ++i)
        {
            const float angle = kTwoPi * static_cast<float>(h) * static_cast<float>(i)
                              / static_cast<float>(size);
            table[i] += amp * std::sin(angle);
        }
    }
}

// Generate one mipmap level for one shape.
// baseFreq controls the band limit: maxHarmonics = floor(nyquist / baseFreq).
void generateTable(float* table, int size, Shape shape, float baseFreq, float sr)
{
    zero(table, size);
    const float nyquist = sr * 0.5f;
    const int maxHarmonics = std::max(1, static_cast<int>(nyquist / std::max(1.0f, baseFreq)));

    switch (shape)
    {
        case Shape::Pulse:
            buildPulse(table, size, maxHarmonics);
            break;
        case Shape::SuperSaw:
            buildSuperSaw(table, size, maxHarmonics);
            break;
        case Shape::Metallic:
            buildMetallic(table, size, baseFreq, sr);
            break;
        case Shape::FormantA:
            // "A" formant — F1=730, F2=1090, F3=2440 (bandwidths in inboil units).
            buildFormant(table, size, baseFreq, sr,
                         730.0f, 80.0f,  1.0f,
                         1090.0f, 100.0f, 0.7f,
                         2440.0f, 120.0f, 0.4f,
                         maxHarmonics);
            break;
        case Shape::FormantO:
            // "O" formant — F1=570, F2=840, F3=2410.
            buildFormant(table, size, baseFreq, sr,
                         570.0f, 70.0f,  1.0f,
                         840.0f, 90.0f,  0.6f,
                         2410.0f, 120.0f, 0.3f,
                         maxHarmonics);
            break;
        default:
            // Sine / Saw / Square / Triangle / Pulse / Spectral — additive series.
            addAdditive(table, size, shape, maxHarmonics);
            break;
    }

    normalise(table, size);
}

} // namespace

// Out-of-line definition of the opaque table-set type forward-declared in the
// header.  Storage is plain by-value arrays so the static cache owns the data
// outright.
struct MultiShapeWavetable::TableSet
{
    std::array<std::array<std::array<float, kTableSize>, kMipmapLevels>, kShapeCount> shapes{};
};

namespace {

// Shared across all instances at the same sample rate.  Init costs ~10 ms
// for all 10 shapes × 9 levels; amortised once per unique sample rate.
// Mutex-guarded at construction; `process()` never touches the cache.

std::mutex& cacheMutex()
{
    static std::mutex m;
    return m;
}

std::unordered_map<float, std::unique_ptr<MultiShapeWavetable::TableSet>>& typedCache()
{
    static std::unordered_map<float, std::unique_ptr<MultiShapeWavetable::TableSet>> c;
    return c;
}

void buildEntry(MultiShapeWavetable::TableSet& entry, float sr)
{
    for (int s = 0; s < kShapeCount; ++s)
    {
        for (int m = 0; m < kMipmapLevels; ++m)
        {
            generateTable(entry.shapes[s][m].data(), kTableSize,
                          static_cast<Shape>(s), kMipmapFreqs[m], sr);
        }
    }
}

} // namespace

const MultiShapeWavetable::TableSet&
MultiShapeWavetable::getOrBuildTables(float sampleRate)
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    auto& c = typedCache();
    auto it = c.find(sampleRate);
    if (it != c.end()) return *it->second;
    auto entry = std::make_unique<TableSet>();
    buildEntry(*entry, sampleRate);
    const TableSet& ref = *entry;
    c.emplace(sampleRate, std::move(entry));
    return ref;
}

int MultiShapeWavetable::mipmapLevelFor(float freqHz)
{
    // Octave-band selector: smallest level k where kMipmapFreqs[k] >= freqHz.
    // The table at level k contains maxHarmonics = floor(nyquist/baseFreq) sines,
    // so the highest playback frequency content is maxHarmonics × freqHz.  With
    // freqHz <= baseFreq[k], that product stays at-or-below nyquist → no aliasing.
    //
    // NOTE: this diverges from inboil melodic.ts L641-647, which picks
    // level k where freq < kMipmapFreqs[k+1].  Under that rule the level's
    // table contains too many harmonics for the playback freq, and harmonics
    // 6..maxHarmonics fold back as audible aliasing (verified empirically at
    // 4000 Hz Saw: bin 4100 Hz held aliased harmonic 10 at -25 dB).  This
    // implementation prioritises alias-free output over inboil parity.
    for (int k = 0; k < kMipmapLevels; ++k)
        if (kMipmapFreqs[k] >= freqHz) return k;
    // freq > 8192 Hz (uncommon — above C9 fundamental): fall back to the
    // highest-baseFreq table and accept some aliasing.
    return kMipmapLevels - 1;
}

void MultiShapeWavetable::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    tables_ = &getOrBuildTables(sampleRate);
    reset();
}

void MultiShapeWavetable::reset()
{
    phase_ = 0.0f;
}

void MultiShapeWavetable::setFrequency(float freqHz)
{
    freqHz = std::clamp(freqHz, 0.0f, sampleRate_ * 0.5f);
    freqHz_ = freqHz;
    phaseInc_ = freqHz / sampleRate_;
}

void MultiShapeWavetable::setShape(Shape s)
{
    setShapePosition(static_cast<float>(s));
}

void MultiShapeWavetable::setShapePosition(float pos)
{
    const float maxPos = static_cast<float>(kShapeCount - 1);
    shapePos_ = std::clamp(pos, 0.0f, maxPos);
}

float MultiShapeWavetable::process()
{
    phase_ += phaseInc_;
    phase_ -= std::floor(phase_);

    if (tables_ == nullptr) return 0.0f;

    const int level = mipmapLevelFor(freqHz_);

    // Shape morph: integer index + fractional crossfade between adjacent shapes.
    const int idxA = static_cast<int>(shapePos_);
    const int idxB = std::min(idxA + 1, kShapeCount - 1);
    const float frac = shapePos_ - static_cast<float>(idxA);

    // Wavetable sample interpolation.  Phase ∈ [0,1) → index ∈ [0, kTableSize).
    const float pos = phase_ * static_cast<float>(kTableSize);
    int i0 = static_cast<int>(pos);
    if (i0 >= kTableSize) i0 = kTableSize - 1;
    const int i1 = (i0 + 1) % kTableSize;
    const float sf = pos - static_cast<float>(i0);

    const auto& tA = tables_->shapes[idxA][level];
    const auto& tB = tables_->shapes[idxB][level];

    const float a = tA[i0] + sf * (tA[i1] - tA[i0]);
    const float b = tB[i0] + sf * (tB[i1] - tB[i0]);

    return a + frac * (b - a);
}

} // namespace ideath
