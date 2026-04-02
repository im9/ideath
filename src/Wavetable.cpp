#include <ideath/Wavetable.h>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ideath {

void Wavetable::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void Wavetable::reset()
{
    phase_ = 0.0f;
    phaseInc_ = 0.0f;
}

void Wavetable::setFrequency(float freqHz)
{
    freqHz = std::clamp(freqHz, 0.0f, sampleRate_ * 0.5f);
    phaseInc_ = freqHz / sampleRate_;
}

void Wavetable::setInterpolation(Interpolation mode)
{
    interpolation_ = mode;
}

void Wavetable::setTable(const float* data, int size)
{
    size = std::clamp(size, 1, kMaxTableSize);
    tableSize_ = size;
    for (int i = 0; i < size; ++i)
        table_[static_cast<size_t>(i)] = (data[i] / 7.5f) - 1.0f;
}

void Wavetable::setTableNormalized(const float* data, int size)
{
    size = std::clamp(size, 1, kMaxTableSize);
    tableSize_ = size;
    for (int i = 0; i < size; ++i)
        table_[static_cast<size_t>(i)] = data[i];
}

float Wavetable::process()
{
    phase_ += phaseInc_;
    phase_ -= std::floor(phase_);

    const float pos = phase_ * static_cast<float>(tableSize_);

    if (interpolation_ == Interpolation::Nearest)
    {
        int index = static_cast<int>(pos);
        if (index >= tableSize_) index = tableSize_ - 1;
        return table_[static_cast<size_t>(index)];
    }

    // Linear interpolation
    int i0 = static_cast<int>(pos);
    if (i0 >= tableSize_) i0 = tableSize_ - 1;
    int i1 = (i0 + 1) % tableSize_;
    float frac = pos - static_cast<float>(i0);
    return table_[static_cast<size_t>(i0)]
         + frac * (table_[static_cast<size_t>(i1)] - table_[static_cast<size_t>(i0)]);
}

// --- Factory methods ---

Wavetable Wavetable::squareTable(int size)
{
    Wavetable wt;
    float buf[kMaxTableSize];
    size = std::clamp(size, 1, kMaxTableSize);
    int half = size / 2;
    for (int i = 0; i < size; ++i)
        buf[i] = (i < half) ? 1.0f : -1.0f;
    wt.setTableNormalized(buf, size);
    return wt;
}

Wavetable Wavetable::sawTable(int size)
{
    Wavetable wt;
    float buf[kMaxTableSize];
    size = std::clamp(size, 1, kMaxTableSize);
    for (int i = 0; i < size; ++i)
        buf[i] = (2.0f * static_cast<float>(i) / static_cast<float>(size)) - 1.0f;
    wt.setTableNormalized(buf, size);
    return wt;
}

Wavetable Wavetable::triangleTable(int size)
{
    Wavetable wt;
    float buf[kMaxTableSize];
    size = std::clamp(size, 1, kMaxTableSize);
    for (int i = 0; i < size; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(size);
        buf[i] = (t < 0.5f)
            ? (4.0f * t - 1.0f)
            : (3.0f - 4.0f * t);
    }
    wt.setTableNormalized(buf, size);
    return wt;
}

Wavetable Wavetable::sineTable(int size)
{
    Wavetable wt;
    float buf[kMaxTableSize];
    size = std::clamp(size, 1, kMaxTableSize);
    for (int i = 0; i < size; ++i)
        buf[i] = std::sin(2.0f * static_cast<float>(M_PI) * static_cast<float>(i)
                          / static_cast<float>(size));
    wt.setTableNormalized(buf, size);
    return wt;
}

} // namespace ideath
