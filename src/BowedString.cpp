#include <ideath/BowedString.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace ideath {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Phase delay (samples) of the one-pole y = (1 − c)·x + c·y[n−1] at ω.
double onePolePhaseDelay(double c, double w)
{
    return std::atan2(c * std::sin(w), 1.0 - c * std::cos(w)) / w;
}

/// Bow-table slope k = −φ'(Δ0) at the sliding rest state, as a function
/// of s = Δ0 / w (Δ0 = bow-relative velocity at rest, w = stick width).
/// For s > 1/4 (slip): φ(x) = x·q^−4 with q = x/w + 3/4, so
/// φ'(x) = q^−5 (3/4 − 3x/w) and k(s) = (s + 3/4)^−5 (3s − 3/4).
double slipSlope(double s)
{
    const double q = s + 0.75;
    return (3.0 * s - 0.75) / (q * q * q * q * q);
}

} // namespace

BowedString::Loop BowedString::computeLoop(float sampleRate, float frequency,
                                           float position, float damping)
{
    const double sr = sampleRate;
    const double f  = std::clamp(frequency, kMinFreq, std::max(kMinFreq, sampleRate * 0.45f));
    const double beta = std::clamp(position, kMinPosition, kMaxPosition);
    const double damp = std::clamp(damping, 0.0f, 1.0f);

    Loop l;
    const double c = kMaxBridgeLowpass * damp;
    l.lowpass = static_cast<float>(c);
    const double w = 2.0 * kPi * f / sr;

    // Loop length: one period minus the low-pass phase delay at f0, so the
    // loop as a whole (both segments + low-pass) is one period.  Reads
    // happen before writes in process(), so the segments add no extra
    // sample.  The one-pole's phase delay is at most its low-frequency
    // limit c/(1 − c) ≤ 0.54 samples, and at the top pitch (P = 2.22,
    // ω0 = 2.83) it is 0.03, so L ≥ 2 across the clamped range and both
    // segments get ≥ 1 sample; the max(2, ·) is only a guard.
    const double P = sr / f;
    const double L = std::max(2.0, P - onePolePhaseDelay(c, w));

    // Bridge segment: β·L rounded to whole samples (moves the bow by at
    // most half a sample), at least kMinBridgeSamples, and leaving ≥ 1
    // sample for the nut segment (which wins at the very top of the pitch
    // range, where L < 3).
    const double bridge = std::max(1.0, std::min(std::max(std::round(beta * L),
                                                          static_cast<double>(kMinBridgeSamples)),
                                                 std::floor(L - 1.0)));
    l.bridgeDelay = static_cast<int>(bridge);

    // Nut segment: the rest, fractional through linear interpolation.  The
    // interpolator's phase delay at ω0 for fraction a is
    // atan(a·sin ω / (1 − a + a·cos ω)) / ω, not a; solving for the a that
    // gives the remaining phase θ exactly: a = sin θ / (sin(ω − θ) + sin θ).
    const double nut = L - bridge;
    const double whole = std::floor(nut);
    const double theta = w * (nut - whole);
    const double a = std::sin(theta) / (std::sin(w - theta) + std::sin(theta));
    l.nutDelayInt = static_cast<int>(whole);
    l.nutDelayFrac = static_cast<float>(a);

    // Bridge reflection gain: −60 dB after `decay` seconds, i.e. after
    // decay·f round trips, compensated for the low-pass's and the
    // interpolator's magnitude at f0 so the fundamental decays at exactly
    // the requested rate whatever the damping and the fraction — up to the
    // kMaxLoopGain ceiling, which binds at high pitch with long decays
    // (above ≈ 1.4 kHz at damping 0); there the ring-out is shorter.
    const double decay = (1.0 - damp) * kMaxDecaySec + damp * kMinDecaySec;
    const double cycles = std::max(1.0, decay * f);
    const double gRaw = std::pow(10.0, -3.0 / cycles);
    const double lpMag = (1.0 - c) / std::sqrt(1.0 - 2.0 * c * std::cos(w) + c * c);
    const double interpMag = std::sqrt((1.0 - a) * (1.0 - a) + a * a
                                       + 2.0 * a * (1.0 - a) * std::cos(w));
    l.loopGain = static_cast<float>(std::min(gRaw / (lpMag * interpMag),
                                             static_cast<double>(kMaxLoopGain)));
    return l;
}

float BowedString::minimumPressure(float sampleRate, float frequency,
                                   float position, float damping)
{
    if (!(sampleRate > 0.0f))
        sampleRate = 44100.0f;
    const Loop l = computeLoop(sampleRate, frequency, position, damping);
    const double f  = std::clamp(frequency, kMinFreq, std::max(kMinFreq, sampleRate * 0.45f));
    const double beta = std::clamp(position, kMinPosition, kMaxPosition);
    const double w0 = 2.0 * kPi * f / sampleRate;

    // Linearised loop about the sliding rest state.  With A = −z^−Dn·I(z)
    // (nut segment + interpolator + nut inversion) and
    // B = −g·LP(z)·z^−Db (bridge segment + low-pass + bridge inversion),
    // and the junction's small-signal gain k = −φ'(Δ0) feeding both
    // outgoing waves, the characteristic equation is
    //
    //   A·B·(1 + 2k) + k·(A + B) = 1.
    //
    // A root sits on the unit circle at z = e^{jω} when
    // k(ω) = (1 − AB) / (2AB + A + B) is real; the rest state first loses
    // stability at the smallest positive such k.
    using C = std::complex<double>;
    const double c = l.lowpass, g = l.loopGain, a = l.nutDelayFrac;
    const int Dn = l.nutDelayInt, Db = l.bridgeDelay;
    auto kAt = [&](double w) {
        const C z1 = std::polar(1.0, -w);
        const C A = -std::polar(1.0, -w * Dn) * ((1.0 - a) + a * z1);
        const C B = -g * ((1.0 - c) / (1.0 - c * z1)) * std::polar(1.0, -w * Db);
        const C AB = A * B;
        return (1.0 - AB) / (2.0 * AB + A + B);
    };

    // The bow drives mode n with weight sin²(π n β), largest near
    // n = 1/(2β); the low-pass and the loop gain make higher modes lossier.
    // Scan up to 8 harmonics past that peak (and at least 16), never past
    // Nyquist, 24 points per harmonic spacing (12 per half-turn of the loop
    // phase, so no sign change of Im k between neighbouring modes is
    // skipped).
    const double nMax = std::max(16.0, 1.0 / (2.0 * beta) + 8.0);
    const bool reachesNyquist = w0 * (nMax + 0.5) >= kPi;
    const double wMax = reachesNyquist ? kPi * 0.9999 : w0 * (nMax + 0.5);
    const int steps = static_cast<int>(std::ceil(24.0 * wMax / w0));

    double kCrit = std::numeric_limits<double>::infinity();
    auto accept = [&](const C& kx) {
        const double mag = std::abs(kx);
        if (kx.real() > 0.0 && mag < 1e6
            && std::fabs(kx.imag()) < 1e-6 * std::max(1.0, mag))
            kCrit = std::min(kCrit, kx.real());
    };
    // At z = −1 both A and B are real, so k(π) is real: a root can reach the
    // unit circle there without Im k changing sign.
    if (reachesNyquist)
        accept(kAt(kPi));

    double wPrev = 1e-9;
    C kPrev = kAt(wPrev);
    for (int i = 1; i <= steps; ++i)
    {
        const double wCur = wMax * static_cast<double>(i) / steps;
        const C kCur = kAt(wCur);
        // A sign change of Im k with Re k > 0 near it is a candidate.  If
        // both ends have Re k ≤ 0, or both lie above the best k found so
        // far, the crossing cannot lower kCrit: skip the refinement.
        const bool signChange = (kPrev.imag() <= 0.0) != (kCur.imag() <= 0.0);
        const bool useful = (kPrev.real() > 0.0 || kCur.real() > 0.0)
                         && std::min(kPrev.real(), kCur.real()) < kCrit;
        if (signChange && useful)
        {
            // Illinois false position on Im k.  A sign change through a pole
            // of k (denominator → 0) is not a root crossing; accept() rejects
            // it by requiring Im k ≈ 0 and |k| finite at the refined point.
            double lo = wPrev, hi = wCur;
            double fLo = kPrev.imag(), fHi = kCur.imag();
            C kx = kCur;
            int side = 0;
            for (int it = 0; it < 30; ++it)
            {
                const double wx = (fHi != fLo) ? hi - fHi * (hi - lo) / (fHi - fLo)
                                               : 0.5 * (lo + hi);
                kx = kAt(wx);
                const double fx = kx.imag();
                if (std::fabs(fx) < 1e-9 * std::max(1.0, std::abs(kx)) || hi - lo < 1e-12)
                    break;
                if ((fx <= 0.0) == (fHi <= 0.0)) { hi = wx; fHi = fx; if (side == -1) fLo *= 0.5; side = -1; }
                else                            { lo = wx; fLo = fx; if (side == +1) fHi *= 0.5; side = +1; }
            }
            accept(kx);
        }
        wPrev = wCur;
        kPrev = kCur;
    }

    // Map k back to pressure.  At rest the string is still (v = 0 at the
    // bow), so Δ0 = v_bow + φ(Δ0); with s = Δ0 / w that is
    // v_bow = Δ0 (1 − q^−4), q = s + 3/4, and w = p·kStickWidth·v_bow gives
    //   p(s) = 1 / (kStickWidth · s · (1 − (s + 3/4)^−4)).
    // As p rises from 0, s falls from ∞ and k(s) = slipSlope(s) rises to
    // its peak k(1/2) = 0.2458; the threshold is the s > 1/2 solution of
    // k(s) = kCrit.  No solution → the rest state never destabilises.
    const double kPeak = slipSlope(0.5);
    if (!(kCrit < kPeak))
        return std::numeric_limits<float>::infinity();
    double lo = 0.5, hi = 1e6;              // k(lo) > kCrit > k(hi)
    for (int it = 0; it < 100; ++it)
    {
        const double mid = std::sqrt(lo * hi);
        if (slipSlope(mid) > kCrit) lo = mid; else hi = mid;
    }
    const double s = std::sqrt(lo * hi);
    const double q = s + 0.75;
    const double q4 = q * q * q * q;
    return static_cast<float>(1.0 / (kStickWidth * s * (1.0 - 1.0 / q4)));
}

float BowedString::minimumPressure() const
{
    return minimumPressure(sampleRate_, frequency_, position_, damping_);
}

void BowedString::prepare(float sampleRate)
{
    sampleRate_ = (sampleRate > 0.0f) ? sampleRate : 44100.0f;

    // Both buffers hold up to one period at the lowest pitch plus the
    // interpolation neighbour; round up to a power of two for masking.
    const int needed = static_cast<int>(std::ceil(sampleRate_ / kMinFreq)) + 4;
    int size = 1;
    while (size < needed) size <<= 1;
    bridgeBuf_.assign(static_cast<size_t>(size), 0.0f);
    nutBuf_.assign(static_cast<size_t>(size), 0.0f);
    mask_ = size - 1;

    bowCoef_ = 1.0f - std::exp(-1.0f / (kBowSmoothingSec * sampleRate_));
    recomputeLoop();
    reset();
}

void BowedString::reset()
{
    std::fill(bridgeBuf_.begin(), bridgeBuf_.end(), 0.0f);
    std::fill(nutBuf_.begin(), nutBuf_.end(), 0.0f);
    writePos_ = 0;
    lpState_ = 0.0f;
    bowSmoothed_ = 0.0f;
}

void BowedString::setFrequency(float hz)
{
    const float clamped = std::clamp(hz, kMinFreq, std::max(kMinFreq, sampleRate_ * 0.45f));
    if (clamped == frequency_)
        return;
    frequency_ = clamped;
    recomputeLoop();
}

void BowedString::setBowVelocity(float v)
{
    bowVelocity_ = std::clamp(v, -1.0f, 1.0f);
}

void BowedString::setPressure(float p)
{
    pressure_ = std::clamp(p, 0.0f, 1.0f);
}

void BowedString::setPosition(float p)
{
    const float clamped = std::clamp(p, kMinPosition, kMaxPosition);
    if (clamped == position_)
        return;
    position_ = clamped;
    recomputeLoop();
}

void BowedString::setDamping(float d)
{
    const float clamped = std::clamp(d, 0.0f, 1.0f);
    if (clamped == damping_)
        return;
    damping_ = clamped;
    recomputeLoop();
}

void BowedString::recomputeLoop()
{
    loop_ = computeLoop(sampleRate_, frequency_, position_, damping_);
}

float BowedString::process()
{
    // Bow-speed smoother.  It approaches its target geometrically, so after
    // a release toward 0 it would sink into denormals within ≈ 0.4 s:
    // flush-to-zero once it is within 1e-9 of the target (−180 dB, far
    // below any audible effect on the stick width).
    bowSmoothed_ += (bowVelocity_ - bowSmoothed_) * bowCoef_;
    if (std::fabs(bowVelocity_ - bowSmoothed_) < 1e-9f)
        bowSmoothed_ = bowVelocity_;

    // 1. Waves arriving at the bow point (read before write: no extra
    //    sample in the loop).
    const float fromBridge = bridgeBuf_[static_cast<size_t>((writePos_ - loop_.bridgeDelay) & mask_)];
    const int   nutIdx     = writePos_ - loop_.nutDelayInt;
    const float n0 = nutBuf_[static_cast<size_t>(nutIdx & mask_)];
    const float n1 = nutBuf_[static_cast<size_t>((nutIdx - 1) & mask_)];
    const float fromNut = n0 + loop_.nutDelayFrac * (n1 - n0);

    // 2. End reflections: bridge −g·LP, nut −1.  (The bridge segment's
    //    buffer holds the wave on its way to the bridge; reading it at the
    //    round-trip delay and reflecting here is the same as reflecting at
    //    the bridge and travelling back.)
    lpState_ = (1.0f - loop_.lowpass) * fromBridge + loop_.lowpass * lpState_ + 1e-25f;
    const float bridgeRefl = -loop_.loopGain * lpState_;
    const float nutRefl    = -fromNut;

    // 3. Bow junction (bow table φ, see class comment).
    const float delta = bowSmoothed_ - (bridgeRefl + nutRefl);
    const float stick = pressure_ * kStickWidth * std::fabs(bowSmoothed_);
    float added = 0.0f;
    if (stick > 1e-12f)
    {
        const float q  = std::fabs(delta) / stick + 0.75f;
        const float q2 = q * q;
        added = delta * std::min(1.0f, 1.0f / (q2 * q2));
    }

    // 4. Outgoing waves: each side passes the wave from the other side
    //    plus the bow's contribution.
    const size_t wp = static_cast<size_t>(writePos_);
    nutBuf_[wp]    = bridgeRefl + added + 1e-25f;
    bridgeBuf_[wp] = nutRefl + added + 1e-25f;
    writePos_ = (writePos_ + 1) & mask_;

    return kOutputGain * fromBridge;
}

} // namespace ideath
