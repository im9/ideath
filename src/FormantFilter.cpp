#include <ideath/FormantFilter.h>
#include <algorithm>
#include <cmath>

namespace ideath {

// Static constexpr member definitions (required pre-C++17 inline)
constexpr float FormantFilter::kFreqs[kNumVowels][3];
constexpr float FormantFilter::kGains[kNumVowels][3];

void FormantFilter::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;
    for (auto& b : band_)
        b.prepare(sampleRate);
    updateFormants();
    reset();
}

void FormantFilter::reset()
{
    for (auto& b : band_)
        b.reset();
}

void FormantFilter::setMorph(float morph)
{
    morph_ = std::clamp(morph, 0.0f, 4.0f);
    updateFormants();
}

void FormantFilter::setResonance(float resonance)
{
    resonance_ = std::clamp(resonance, 0.0f, 1.0f);
    updateFormants();
}

void FormantFilter::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void FormantFilter::updateFormants()
{
    // Interpolate between two adjacent vowels
    int lower = static_cast<int>(morph_);
    int upper = lower + 1;
    if (lower >= kNumVowels - 1)
    {
        lower = kNumVowels - 1;
        upper = lower;
    }
    float frac = morph_ - static_cast<float>(lower);

    // Map resonance 0–1 to SVFilter resonance 0.3–0.95
    // Higher resonance = sharper, more vocal peaks
    float svfRes = 0.3f + resonance_ * 0.65f;

    for (int i = 0; i < 3; ++i)
    {
        float freq = kFreqs[lower][i] + frac * (kFreqs[upper][i] - kFreqs[lower][i]);
        gain_[i] = kGains[lower][i] + frac * (kGains[upper][i] - kGains[lower][i]);

        band_[i].setCutoff(freq);
        band_[i].setResonance(svfRes);
        band_[i].setMode(SVFilter::Mode::Bandpass);
    }
}

float FormantFilter::process(float input)
{
    float wet = 0.0f;
    for (int i = 0; i < 3; ++i)
        wet += band_[i].process(input) * gain_[i];

    return input * (1.0f - mix_) + wet * mix_;
}

} // namespace ideath
