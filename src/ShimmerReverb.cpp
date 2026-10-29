#include <ideath/HallReverb.h>
#include <ideath/ShimmerReverb.h>
#include <algorithm>
#include <cmath>

namespace ideath {

static constexpr float kPi = 3.141592654f;

// Grain period 100 ms: long enough that the grain rate (10 Hz, 40 Hz with
// four overlapping grains) sits below the roughness band; the old 43 ms
// two-grain shifter put its window rate at 47 Hz, heard as a buzz.
static constexpr float kGrainSec = 0.1f;
// Splice search: a new grain may start anywhere in a 12 ms span (one
// period down to 83 Hz) and takes the start that best matches the grain
// at its window peak, over a 5 ms correlation window.  Grains then add
// in phase and the voice is a clean tone at exactly f·r.  (Random grain
// starts would put each grain at a random phase and smear the voice over
// the window's main lobe, ±20 Hz — ±75 cents at 220 Hz.)
static constexpr float kSearchSec = 0.012f;
static constexpr float kCorrSec = 0.005f;
static constexpr int kCoarseStep = 4;
// Four Hann grains at 75 % overlap: Σw = 2 and Σw² = 1.5 at every instant.
// The grains read the source at power P_in ≈ Σw²x² / Σw² (x = each grain's
// raw read); their sum has power (Σw·x)², which is 4·P_in when they are in phase (a tone) and
// 1.5·P_in when they are decorrelated (diffuse material).  Both quantities
// come from the same grain reads at the same instant, so their smoothed
// ratio gives the gain that makes the voice unity-power for any material,
// short notes included.  (A running ratio of output power to *input*
// power cannot: the output lags the input by up to the run-up, 300 ms at
// +24, and a 50 ms note came out between −6 and +5 dB.)  The gain is
// clamped to its physical range [1/2, 1/√1.5] so grains that happen to
// cancel are never lifted.  Smoothed over 50 ms — steady at the 40 Hz
// grain rate.
static constexpr float kGrainPowerSum = 1.5f; // Σw²
static constexpr float kMinGrainGain = 0.5f;          // in phase: 1 / Σw
static constexpr float kMaxGrainGain = 0.81649658f;   // decorrelated: 1 / √Σw²
static constexpr float kGrainPowerTauSec = 0.05f;
static constexpr float kAntiDenormal = 1e-25f;
// High-pass on the voices' source.  Pitch shifting maps DC to DC, and the
// FDN's DC response is 1/(1 − g) (all lines in phase) — ≈ 27× at size 0.5
// — so without it the Feedback loop runs away at DC.  20 Hz removes that
// fixed point and stays below the audio band (a 60 Hz corner would take
// 2.3 dB of shifted energy from a pink-noise tail).
static constexpr float kSourceHpHz = 20.0f;

// Upper bound of the detune ratio 2^(kMaxDetuneCents/1200): a voice whose
// nominal ratio times this exceeds 1 can run its read head faster than the
// write head at some point of a grain, so it needs a run-up.
static float detuneCeiling()
{
    return std::exp2(ShimmerReverb::kMaxDetuneCents / 1200.0f);
}

// Energy ratio per generation at amount 1.  Feedback 0.5: each climb is
// 3 dB below the one before (a long but converging cascade).  Input 1.0:
// the shifted copy enters as loud as the dry input.
static constexpr float kRhoMaxFeedback = 0.5f;
static constexpr float kRhoMaxInput = 1.0f;

// Anti-alias low-pass for upward voices: 4th-order Butterworth at
// 0.35·sr / ratio (0.7 × the shifted Nyquist).  Content at the shifted
// Nyquist is ≥ 12 dB down and what folds furthest is down far more.
static constexpr float kAaFraction = 0.35f;
static constexpr float kButterQ1 = 0.5411961f;
static constexpr float kButterQ2 = 1.3065630f;
// Low cut after downward voices: keeps an octave-down copy of the tail
// from muddying the low end.
static constexpr float kLowCutHz = 120.0f;

ShimmerReverb::ShimmerReverb()
{
    voices_[0].semitones = 12.0f;
    voices_[0].level = 1.0f;
    voices_[1].semitones = 19.0f;
    voices_[1].level = 0.0f;
    prepare(44100.0f);
}

void ShimmerReverb::prepare(float sampleRate)
{
    sampleRate_ = sampleRate;

    int size = 1;
    while (size < static_cast<int>(std::ceil(kMaxPreDelay * sampleRate)) + 1)
        size <<= 1;
    preDelay_.assign(static_cast<size_t>(size), 0.0f);
    preDelayMask_ = size - 1;

    grainSamples_ = kGrainSec * sampleRate;
    searchSamples_ = static_cast<int>(kSearchSec * sampleRate);
    corrSamples_ = std::max(16, static_cast<int>(kCorrSec * sampleRate));
    grainPowerCoeff_ = std::exp(-1.0f / (kGrainPowerTauSec * sampleRate));
    sourceHp_.prepare(kSourceHpHz, sampleRate);

    // Largest read delay: an upward grain starts up to (r·c − 1)·P plus the
    // search span behind the write head (r = 4 at +24, c = detune ceiling),
    // and the correlation window looks corrSamples_ further back.
    const float maxRatio = 4.0f * std::pow(2.0f, kMaxDetuneCents / 1200.0f);
    const int maxDelay = static_cast<int>(std::ceil((maxRatio - 1.0f) * grainSamples_))
                       + searchSamples_ + corrSamples_ + 8;
    int vsize = 1;
    while (vsize < maxDelay)
        vsize <<= 1;
    for (Voice& v : voices_)
    {
        v.buffer.assign(static_cast<size_t>(vsize), 0.0f);
        v.reference.assign(static_cast<size_t>(corrSamples_), 0.0f);
        v.mask = vsize - 1;
        setVoiceRatio(v, v.semitones);
    }

    window_.resize(kWindowSize + 1);
    for (int i = 0; i <= kWindowSize; ++i)
        window_[static_cast<size_t>(i)] =
            0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / kWindowSize);

    preDelaySamples_ = static_cast<int>(preDelaySec_ * sampleRate);
    tank_.prepare(sampleRate);
    tank_.setDecay(HallReverb::sizeToT60(size_));
    reset();
    updateShimmer();
}

void ShimmerReverb::reset()
{
    std::fill(preDelay_.begin(), preDelay_.end(), 0.0f);
    preDelayWrite_ = 0;
    sourceHp_.reset();
    for (Voice& v : voices_)
    {
        std::fill(v.buffer.begin(), v.buffer.end(), 0.0f);
        v.write = 0;
        for (int k = 0; k < Voice::kGrains; ++k)
        {
            // Grains start staggered by a quarter period, each at the
            // delay it would have reached at that phase.
            v.phase[static_cast<size_t>(k)] = static_cast<float>(k) / Voice::kGrains;
            const float rc = v.ratio * detuneCeiling();
            const float start = rc > 1.0f ? 2.0f + (rc - 1.0f) * grainSamples_ : 2.0f;
            v.delay[static_cast<size_t>(k)] =
                start + (1.0f - v.ratio) * v.phase[static_cast<size_t>(k)] * grainSamples_;
        }
        v.aa1.reset();
        v.aa2.reset();
        v.hp.reset();
        v.sumPower = 0.0f;
        v.readPower = 0.0f;
    }
    tank_.reset();
}

void ShimmerReverb::setSize(float size)
{
    const float s = std::clamp(size, 0.0f, 1.0f);
    if (s == size_)
        return;
    size_ = s;
    tank_.setDecay(HallReverb::sizeToT60(size_));
    updateShimmer();
}

void ShimmerReverb::setDamp(float damp)
{
    // Hosts call setters every sample; only a real change reaches the
    // tank's coefficient update and the level normalisation.
    const float d = std::clamp(damp, 0.0f, 1.0f);
    if (d == damp_)
        return;
    damp_ = d;
    tank_.setDamp(d);
    updateShimmer();
}

void ShimmerReverb::setShimmer(float amount)
{
    const float a = std::clamp(amount, 0.0f, 1.0f);
    if (a == shimmer_)
        return;
    shimmer_ = a;
    updateShimmer();
}

void ShimmerReverb::setMix(float mix)
{
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void ShimmerReverb::setFreeze(bool freeze)
{
    if (freeze == freeze_)
        return;
    freeze_ = freeze;
    tank_.setFreeze(freeze);
    updateShimmer();
}

void ShimmerReverb::setPreDelay(float seconds)
{
    const float s = std::clamp(seconds, 0.0f, kMaxPreDelay);
    // Seconds are kept so prepare() at another rate keeps the time.
    preDelaySec_ = s;
    preDelaySamples_ = static_cast<int>(s * sampleRate_);
}

void ShimmerReverb::setModDepth(float depth)
{
    tank_.setModDepth(depth);
}

void ShimmerReverb::setModRate(float rateHz)
{
    tank_.setModRate(rateHz);
}

void ShimmerReverb::setVoicePitch(int voice, float semitones)
{
    if (voice < 0 || voice >= kNumVoices)
        return;
    const float s = std::clamp(semitones, -kMaxSemitones, kMaxSemitones);
    Voice& v = voices_[static_cast<size_t>(voice)];
    if (s == v.semitones)
        return;
    setVoiceRatio(v, s);
    updateShimmer();
}

void ShimmerReverb::setVoiceLevel(int voice, float level)
{
    if (voice < 0 || voice >= kNumVoices)
        return;
    const float l = std::clamp(level, 0.0f, 1.0f);
    Voice& v = voices_[static_cast<size_t>(voice)];
    if (l == v.level)
        return;
    v.level = l;
    updateShimmer();
}

void ShimmerReverb::setMode(Mode mode)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    updateShimmer();
}

void ShimmerReverb::setVoiceRatio(Voice& v, float semitones)
{
    // Changing direction brings in a filter that sat idle: clear it.
    const bool wasUp = v.ratio > 1.0f, wasDown = v.ratio < 1.0f;
    v.semitones = semitones;
    v.ratio = std::pow(2.0f, semitones / 12.0f);
    if ((v.ratio > 1.0f) != wasUp || (v.ratio < 1.0f) != wasDown)
    {
        v.aa1.reset();
        v.aa2.reset();
        v.hp.reset();
    }
    if (v.ratio > 1.0f)
    {
        const float fc = std::min(kAaFraction * sampleRate_ / v.ratio, 0.45f * sampleRate_);
        v.aa1.setLowpass(fc, kButterQ1, sampleRate_);
        v.aa2.setLowpass(fc, kButterQ2, sampleRate_);
    }
    if (v.ratio < 1.0f)
        v.hp.setHighpass(kLowCutHz, 0.7071f, sampleRate_);
    v.pinkGain = voicePinkGain(v.ratio);
}

// Second-order RBJ magnitudes (bilinear, analog prototype at the warped
// frequency): low-pass 1 / ((1 − Ω²)² + (Ω/Q)²), high-pass Ω⁴ / (…).
static float rbjMag2(float hz, float fc, float q, bool highpass, float sampleRate)
{
    const float o = std::tan(kPi * hz / sampleRate) / std::tan(kPi * fc / sampleRate);
    const float den = (1.0f - o * o) * (1.0f - o * o) + (o / q) * (o / q);
    return (highpass ? o * o * o * o : 1.0f) / den;
}

float ShimmerReverb::voicePinkGain(float ratio) const
{
    // Share of a pink source's energy the voice's filters keep: source
    // 20 Hz blocker, anti-alias (upward) or 120 Hz low-cut after the shift
    // (downward).  Pink = equal energy per octave → mean over a
    // third-octave grid of source frequencies; anything shifted past
    // Nyquist counts as lost.
    const float aaHz = std::min(kAaFraction * sampleRate_ / ratio, 0.45f * sampleRate_);
    float sum = 0.0f;
    int count = 0;
    for (float hz = 25.0f; hz <= std::min(16000.0f, 0.45f * sampleRate_); hz *= 1.2599210f)
    {
        const float o = hz / kSourceHpHz;
        float g = o * o / (1.0f + o * o);
        if (ratio > 1.0f)
            g *= rbjMag2(hz, aaHz, kButterQ1, false, sampleRate_)
               * rbjMag2(hz, aaHz, kButterQ2, false, sampleRate_);
        const float shifted = hz * ratio;
        if (shifted >= 0.5f * sampleRate_)
            g = 0.0f;
        else if (ratio < 1.0f)
            g *= rbjMag2(shifted, kLowCutHz, 0.7071f, true, sampleRate_);
        sum += g;
        ++count;
    }
    return count > 0 ? sum / static_cast<float>(count) : 1.0f;
}

void ShimmerReverb::updateShimmer()
{
    float power = 0.0f;
    for (const Voice& v : voices_)
        power += v.level * v.level;
    voiceNorm_ = power > 1.0f ? 1.0f / std::sqrt(power) : 1.0f;
    const float pv = std::min(1.0f, power);

    const float rhoMax = mode_ == Mode::Feedback ? kRhoMaxFeedback : kRhoMaxInput;
    rho_ = shimmer_ * shimmer_ * rhoMax * pv;
    // κ: the share of a pink generation's energy the voices' filters pass
    // (level-weighted).  It only lowers the energy that comes back, so it
    // enters the level normalisation, never γ.
    float kappa = 1.0f;
    if (power > 0.0f)
    {
        kappa = 0.0f;
        for (const Voice& v : voices_)
            kappa += v.level * v.level * v.pinkGain;
        kappa /= power;
    }

    if (mode_ == Mode::Feedback)
    {
        // The voices read a tank tap whose energy is G = tank power gain
        // times the energy that went in.  Injecting γ·shift(tap) adds
        // γ²·G·P_v of the source generation's energy; set that to ρ:
        //   γ = amount · √(ρ_max / G).
        // Frozen: G = ∞ → γ = 0, nothing new is shifted into the tank.
        const float g = tank_.powerGain();
        inputShiftGain_ = std::isfinite(g) ? shimmer_ * std::sqrt(rhoMax / g) : 0.0f;
        // Generations carry 1, ρκλ, (ρκλ)², … of the energy → Σ = 1/(1 − ρκλ).
        // λ = G_pink / G: γ is set from the low-frequency gain G, but a
        // generation's energy comes back through the gain its spectrum
        // sees, which damping lowers.  Pink weighting matches the level
        // spec; γ itself is left alone so darker material can only fall
        // short of ρ, never exceed it.
        // Frozen (G = ∞): keep the last λ so engaging freeze does not step
        // the level.
        if (std::isfinite(g))
            lambda_ = std::clamp(tank_.pinkPowerGain() / g, 0.0f, 1.0f);
        outNorm_ = std::sqrt(1.0f - rho_ * kappa * lambda_);
    }
    else
    {
        // One shifted copy at ρκ of the input energy.
        inputShiftGain_ = shimmer_ * std::sqrt(rhoMax);
        outNorm_ = 1.0f / std::sqrt(1.0f + rho_ * kappa);
    }
}

int ShimmerReverb::bestSplice(Voice& v, int grain, int lo, int hi) const
{
    // Start delay in [lo, hi] whose recent past best matches what the other
    // three grains are playing now, weighted by their windows.  Matching
    // only one of them (the one at its window peak) leaves the quarter-
    // period neighbours unconstrained: two families of grains can settle in
    // antiphase, and a sustained tone came out 24 dB down.  Coarse pass
    // every kCoarseStep samples, then a fine pass around the winner.
    float* ref = v.reference.data();
    for (int j = 0; j < corrSamples_; ++j)
        ref[j] = 0.0f;
    for (int m = 0; m < Voice::kGrains; ++m)
    {
        if (m == grain)
            continue;
        const float w = window_[static_cast<size_t>(v.phase[static_cast<size_t>(m)] * kWindowSize)];
        const int dm = static_cast<int>(v.delay[static_cast<size_t>(m)]);
        for (int j = 0; j < corrSamples_; ++j)
            ref[j] += w * v.buffer[static_cast<size_t>((v.write - dm - j) & v.mask)];
    }

    auto score = [&](int d) {
        float dot = 0.0f, energy = 1e-12f;
        for (int j = 0; j < corrSamples_; ++j)
        {
            const float b = v.buffer[static_cast<size_t>((v.write - d - j) & v.mask)];
            dot += ref[j] * b;
            energy += b * b;
        }
        return dot / std::sqrt(energy);
    };
    int best = lo;
    float bestScore = -INFINITY;
    for (int d = lo; d <= hi; d += kCoarseStep)
    {
        const float sc = score(d);
        if (sc > bestScore) { bestScore = sc; best = d; }
    }
    const int from = std::max(lo, best - kCoarseStep + 1);
    const int to = std::min(hi, best + kCoarseStep - 1);
    for (int d = from; d <= to; ++d)
    {
        const float sc = score(d);
        if (sc > bestScore) { bestScore = sc; best = d; }
    }
    return best;
}

float ShimmerReverb::processVoice(Voice& v, float input, float detuneCents)
{
    float x = input;
    if (v.ratio > 1.0f)
        x = v.aa2.process(v.aa1.process(x));
    v.buffer[static_cast<size_t>(v.write)] = x;

    // Detune by the tank LFO: the read head runs at r·2^(c/1200).  |c| ≤ 8
    // cents, so 2^(c/1200) = e^y with |y| ≤ 0.0046; the second-order series
    // 1 + y + y²/2 is off by y³/6 ≤ 2·10⁻⁸ (far below a cent) and saves an
    // exp2 per voice per sample.
    const float y = detuneCents * (0.69314718f / 1200.0f);
    const float r = v.ratio * (1.0f + y + 0.5f * y * y);
    const float step = 1.0f / grainSamples_;
    const float maxDelay = static_cast<float>(v.mask) - 2.0f;

    // A silent voice (level 0) keeps its history and grain clock running —
    // so raising its level later is seamless — but skips the reads and
    // the splice search.
    const bool silent = v.level == 0.0f;

    float out = 0.0f;
    float readPower = 0.0f; // Σ (w·x)², x = each grain's raw read
    for (int k = 0; k < Voice::kGrains; ++k)
    {
        float& d = v.delay[static_cast<size_t>(k)];
        float& ph = v.phase[static_cast<size_t>(k)];

        if (!silent)
        {
            // Linear read `d` samples behind the write head.
            const int di = static_cast<int>(d);
            const float f = d - static_cast<float>(di);
            const float a = v.buffer[static_cast<size_t>((v.write - di) & v.mask)];
            const float b = v.buffer[static_cast<size_t>((v.write - di - 1) & v.mask)];
            const float pos = ph * kWindowSize;
            const int wi = static_cast<int>(pos);
            const float wf = pos - static_cast<float>(wi);
            const float w = window_[static_cast<size_t>(wi)]
                          + wf * (window_[static_cast<size_t>(wi + 1)] - window_[static_cast<size_t>(wi)]);
            const float wx = w * (a + f * (b - a));
            out += wx;
            readPower += wx * wx;
        }

        // Read head moves r samples per sample → the delay changes by 1 − r.
        d = std::clamp(d + 1.0f - r, 2.0f, maxDelay);
        ph += step;
        if (ph >= 1.0f)
        {
            ph -= std::floor(ph);
            // New grain (window at zero, so the jump is silent).  An upward
            // grain needs a run-up of (r·c − 1)·P to stay behind the write
            // head for a whole period; within the search span after that,
            // start where it best matches the grains already playing, so
            // all four add in phase.
            const float rc = v.ratio * detuneCeiling();
            const int runUp = rc > 1.0f
                ? static_cast<int>(std::ceil((rc - 1.0f) * grainSamples_))
                : 0;
            const int lo = 2 + runUp;
            const int hi = std::min(lo + searchSamples_,
                                    static_cast<int>(maxDelay) - corrSamples_);
            d = silent ? static_cast<float>(lo)
                       : static_cast<float>(bestSplice(v, k, lo, std::max(lo, hi)));
        }
    }
    v.write = (v.write + 1) & v.mask;
    if (silent)
        return 0.0f;

    v.sumPower = out * out + (v.sumPower - out * out) * grainPowerCoeff_ + kAntiDenormal;
    const float pIn = readPower / kGrainPowerSum;
    v.readPower = pIn + (v.readPower - pIn) * grainPowerCoeff_ + kAntiDenormal;
    out *= std::clamp(std::sqrt(v.readPower / v.sumPower), kMinGrainGain, kMaxGrainGain);
    if (v.ratio < 1.0f)
        out = v.hp.process(out);
    return out;
}

std::pair<float, float> ShimmerReverb::process(float input)
{
    preDelay_[static_cast<size_t>(preDelayWrite_)] = input;
    const float delayed =
        preDelay_[static_cast<size_t>((preDelayWrite_ - preDelaySamples_) & preDelayMask_)];
    preDelayWrite_ = (preDelayWrite_ + 1) & preDelayMask_;

    // Frozen: the tank is lossless, so no input and no freshly shifted
    // sound is fed (fresh shifting is what would make the held pitch climb
    // forever in Feedback mode).  Only the input diffuser's last ≈ 30 ms
    // drains in after the switch.
    const float x = freeze_ ? 0.0f : delayed;

    const float source = sourceHp_.process(mode_ == Mode::Feedback ? tank_.monoTap() : x);
    const float detune = tank_.lfo() * tank_.modDepth() * kMaxDetuneCents;
    float shifted = 0.0f;
    shifted += voices_[0].level * processVoice(voices_[0], source, detune);
    shifted += voices_[1].level * processVoice(voices_[1], source, -detune);
    shifted *= voiceNorm_;

    const float inject = freeze_ ? 0.0f : x + inputShiftGain_ * shifted;
    auto [l, r] = tank_.process(inject);

    const float dry = input * (1.0f - mix_);
    const float wet = mix_ * outNorm_;
    return { dry + l * wet, dry + r * wet };
}

} // namespace ideath
