#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ideath/TapeDelay.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr float kSampleRate = 44100.0f;

static float rms(const std::vector<float>& buf)
{
    double sum = 0.0;
    for (float s : buf)
        sum += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(buf.size())));
}

// Threshold derivations used throughout this file
// -----------------------------------------------
// TapeDelay pipeline (after Oct-2026 fix):
//   wow/flutter modulate delayTime → modulatedDelay ∈ [1, maxDelaySamples]
//   wet = readDelay(modulatedDelay)        // int-index + small-magnitude fraction
//   colored = hp(wet) → lp → tanh(·drive)  // tape coloring on PLAYBACK
//   buffer[writePos] = input + colored·fb + 1e-25f
//   return input·(1 − mix) + colored·mix
//
// Key bounds from this topology (|input| ≤ 1):
//   - tanh(·) ∈ [-1, 1], so colored ∈ [-1, 1] regardless of drive.
//   - With mix = 1 the output is `colored` → |out| ≤ 1 always.
//   - With mix = 0 the output is bit-exact `input`.
//
// Pre-delay silence (fb = 0, wow = flutter = 0): before the impulse
// emerges at call D, wet read from unwritten buffer slots returns 0.
// However, Biquad::process internally adds kAntiDenormal = 1e-25f to
// its state z1_ every sample, so HP/LP output of exact-zero input
// ramps up to ~1e-25 over a few calls. This is below float ULP of any
// audible signal but precludes bit-exact zero comparison. All "silent
// output" assertions below use 1e-20 absolute tolerance (5×10⁵ the
// anti-denormal scale, still −400 dB below audibility).
//
// Colored impulse at delay location (default HP 40 Hz, LP 8 kHz,
// Q = 0.707, drive = 1.5, fs = 44.1 kHz): RBJ HP has b0 ≈ 1, RBJ LP has
// b0 = (1 − cos(2π·8000/44100))/2 ≈ 0.291. First sample of the cascade
// for input 1: tanh(0.291 · 1.5) ≈ 0.259. Filter ringing peaks on the
// second sample at ≈ 0.58 before decaying. Within the 5-sample window
// tested, max |out| ≈ 0.58 — tight lower bound 0.4 catches regressions
// that skip coloring or blow up drive/feedback.
//
// Boundedness with hot feedback + drive: the tanh on the playback path
// clamps `colored` to [-1, 1] per sample. Output = colored · mix ≤ 1
// in absolute value. ±1 + 1e-5 is a comfortable ULP bound.

TEST_CASE("TapeDelay: dry mix bypasses delay (bit-exact)", "[tapedelay]")
{
    // mix = 0 → output = input · 1 + colored · 0 = input exactly.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.2f);
    delay.setMix(0.0f);

    for (int i = 0; i < 100; ++i)
    {
        const float input = static_cast<float>(i) / 100.0f;
        REQUIRE(delay.process(input) == input);
    }
}

TEST_CASE("TapeDelay: impulse emerges at base delay with expected coloring", "[tapedelay]")
{
    // fb = 0, wow = flutter = 0, mix = 1, default filters + drive.
    // Delay = 0.05 s · 44.1 kHz = 2205 samples exactly.
    // Calls 1..D-1 produce bit-exact 0 (wet = 0, filter state stays 0).
    // Call D: wet = buf[0] = 1, colored = tanh(LP(HP(1)) · drive) ≈ 0.259.
    // The filter's second sample rings to ≈ 0.58 (analytical via RBJ
    // biquad impulse response).
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.05f);
    delay.setFeedback(0.0f);
    delay.setMix(1.0f);
    delay.setWowDepth(0.0f);
    delay.setFlutterDepth(0.0f);

    constexpr int kDelay = 2205;
    delay.process(1.0f);  // call 0: impulse in

    for (int i = 1; i < kDelay; ++i)
    {
        INFO("i = " << i);
        REQUIRE(std::fabs(delay.process(0.0f)) < 1e-20f);
    }

    // Impulse window: 5 samples starting at kDelay. The first sample lands
    // at ≈ 0.259, the second (filter ring peak) at ≈ 0.58.
    float maxAbs = 0.0f;
    for (int i = 0; i < 5; ++i)
        maxAbs = std::max(maxAbs, std::fabs(delay.process(0.0f)));
    REQUIRE(maxAbs > 0.4f);
    REQUIRE(maxAbs <= 1.0f);
}

TEST_CASE("TapeDelay: wow/flutter modulation changes output relative to static", "[tapedelay]")
{
    // Same impulse + feedback into two instances, one static and one with
    // wow (0.5 Hz, 3 ms depth) + flutter (5 Hz, 0.8 ms depth). Buffered
    // outputs diverge because the modulated read position differs every
    // sample. Cumulative |a − b| > 0.1 over 5000 samples catches a
    // regression where modulation is silently disabled.
    ideath::TapeDelay staticDelay;
    staticDelay.prepare(kSampleRate, 1.0f);
    staticDelay.setDelay(0.03f);
    staticDelay.setFeedback(0.6f);
    staticDelay.setMix(1.0f);
    staticDelay.setWowDepth(0.0f);
    staticDelay.setFlutterDepth(0.0f);

    ideath::TapeDelay modDelay;
    modDelay.prepare(kSampleRate, 1.0f);
    modDelay.setDelay(0.03f);
    modDelay.setFeedback(0.6f);
    modDelay.setMix(1.0f);
    modDelay.setWowDepth(0.003f);
    modDelay.setWowRate(0.5f);
    modDelay.setFlutterDepth(0.0008f);
    modDelay.setFlutterRate(5.0f);

    std::vector<float> a(5000), b(5000);
    for (int i = 0; i < 5000; ++i)
    {
        const float input = (i == 0) ? 1.0f : 0.0f;
        a[static_cast<size_t>(i)] = staticDelay.process(input);
        b[static_cast<size_t>(i)] = modDelay.process(input);
    }

    float diff = 0.0f;
    for (int i = 0; i < 5000; ++i)
        diff += std::fabs(a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]);
    REQUIRE(diff > 0.1f);
}

TEST_CASE("TapeDelay: dark feedback filter has strictly less RMS than bright", "[tapedelay]")
{
    // Both instances impulsed once with fb = 0.8, mix = 1. Dark filters
    // (HP 200, LP 1500) apply per playback, so each echo loses high and
    // low content. Bright filters (HP 20, LP 18000) pass ≈ all content.
    // Analytical first-sample colored: bright ≈ 0.76, dark ≈ 0.015 —
    // two orders of magnitude apart. Test requires dark RMS < bright RMS
    // by at least 2× (conservative vs the ≈ 50× analytical ratio).
    auto runDelay = [](float hp, float lp) {
        ideath::TapeDelay d;
        d.prepare(kSampleRate, 1.0f);
        d.setDelay(0.02f);
        d.setFeedback(0.8f);
        d.setMix(1.0f);
        d.setLowpass(lp);
        d.setHighpass(hp);
        d.setWowDepth(0.0f);
        d.setFlutterDepth(0.0f);
        d.process(1.0f);
        std::vector<float> buf(6000);
        for (int i = 0; i < 6000; ++i) buf[static_cast<size_t>(i)] = d.process(0.0f);
        return rms(buf);
    };

    const float rmsBright = runDelay(20.0f, 18000.0f);
    const float rmsDark   = runDelay(200.0f, 1500.0f);
    REQUIRE(rmsDark < rmsBright);
    REQUIRE(rmsDark < rmsBright * 0.5f);
}

TEST_CASE("TapeDelay: reset clears state (bit-exact zero output)", "[tapedelay]")
{
    // reset() zeros the buffer, resets filter state, and rewinds writePos
    // and modulation phase. Subsequent 0 input yields 0 output exactly.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.1f);
    delay.setFeedback(0.8f);
    delay.setMix(1.0f);

    for (int i = 0; i < 4000; ++i) delay.process(0.8f);
    delay.reset();

    for (int i = 0; i < 200; ++i)
        REQUIRE(std::fabs(delay.process(0.0f)) < 1e-20f);
}

TEST_CASE("TapeDelay: output bounded to [-1, 1] at hot feedback + drive", "[tapedelay]")
{
    // tanh clamps colored to [-1, 1] per sample; mix = 1 → output =
    // colored. Regardless of feedback amount or drive, |output| ≤ 1.
    // 1 s of modulated sine burst covers the attack and release phases.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.03f);
    delay.setFeedback(0.95f);
    delay.setMix(1.0f);
    delay.setDrive(4.0f);
    delay.setWowDepth(0.002f);
    delay.setFlutterDepth(0.0005f);

    for (int i = 0; i < 44100; ++i)
    {
        const float input = (i < 128) ? std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / kSampleRate) : 0.0f;
        const float output = delay.process(input);
        REQUIRE(std::isfinite(output));
        REQUIRE(std::fabs(output) <= 1.0f + 1e-5f);
    }
}

TEST_CASE("TapeDelay: wow/flutter modulated delay stays clamped above 1 sample", "[tapedelay]")
{
    // Extreme wow depth would push delay below 1 sample; the impl clamps
    // modulatedDelay to [1, maxDelaySamples]. Test that no NaN/Inf escape
    // and output stays bounded.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 0.1f);
    delay.setDelay(0.0005f);    // ~22 samples — close to the lower clamp
    delay.setFeedback(0.5f);
    delay.setMix(1.0f);
    delay.setWowDepth(0.001f);  // ~44 samples, can overshoot delay
    delay.setWowRate(3.0f);
    delay.setFlutterDepth(0.0005f);
    delay.setFlutterRate(8.0f);

    for (int i = 0; i < 44100; ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t);
        const float out = delay.process(input);
        REQUIRE(std::isfinite(out));
        REQUIRE(std::fabs(out) <= 1.0f + 1e-5f);
    }
}

// ---------------------------------------------------------------------------
// Long-run stability
// ---------------------------------------------------------------------------

TEST_CASE("TapeDelay: 10-second stability at aggressive settings", "[tapedelay][stability]")
{
    // 10 s × 44.1 kHz = 441 000 samples. High feedback + drive, full
    // modulation. Outputs must stay finite and within the tanh-bounded
    // range; buffer/filter state must not diverge.
    ideath::TapeDelay delay;
    delay.prepare(kSampleRate, 1.0f);
    delay.setDelay(0.2f);
    delay.setFeedback(0.9f);
    delay.setMix(1.0f);
    delay.setDrive(3.0f);
    delay.setWowDepth(0.003f);
    delay.setFlutterDepth(0.001f);
    delay.setLowpass(4000.0f);
    delay.setHighpass(80.0f);

    for (int i = 0; i < 441000; ++i)
    {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 0.3f * std::sin(2.0f * 3.14159265f * 110.0f * t);
        const float out = delay.process(input);
        REQUIRE(std::isfinite(out));
        REQUIRE(std::fabs(out) <= 1.0f + 1e-5f);
    }
}

// ---------------------------------------------------------------------------
// Stereo output, clean (unsaturated) repeats, diffusion
// ---------------------------------------------------------------------------

static int argmaxAbs(const std::vector<float>& x)
{
    int best = 0;
    for (int i = 1; i < static_cast<int>(x.size()); ++i)
        if (std::fabs(x[i]) > std::fabs(x[best]))
            best = i;
    return best;
}

// Clean digital repeats: a typical DIGITAL setting (LP wide open,
// HP 20 Hz) with saturation off.
static void cleanDelay(ideath::TapeDelay& d, float delaySec, float fb)
{
    d.prepare(kSampleRate, 1.0f);
    d.setDelay(delaySec);
    d.setFeedback(fb);
    d.setMix(1.0f);
    d.setLowpass(20000.0f);
    d.setHighpass(20.0f);
    d.setSaturation(false);
}

TEST_CASE("TapeDelay: stereo Offset mode delays the right channel by the offset", "[tapedelay]")
{
    // Impulse, no feedback: L echo at D, R echo at D + offset.  ±1 sample
    // covers the HP/LP group delay (< 1 sample at 20 Hz / 20 kHz corners
    // near the echo peak).
    ideath::TapeDelay d;
    cleanDelay(d, 0.1f, 0.0f);
    d.setStereoMode(ideath::TapeDelay::StereoMode::Offset);
    d.setStereoOffset(0.02f);
    std::vector<float> l(8000), r(8000);
    for (int i = 0; i < 8000; ++i)
    {
        auto [a, b] = d.processStereo(i == 0 ? 1.0f : 0.0f);
        l[i] = a;
        r[i] = b;
    }
    REQUIRE(std::abs(argmaxAbs(l) - 4410) <= 1);
    REQUIRE(std::abs(argmaxAbs(r) - (4410 + 882)) <= 1);
}

TEST_CASE("TapeDelay: stereo Offset mode left channel equals mono process", "[tapedelay]")
{
    // The left channel is the mono loop itself, bit for bit.
    ideath::TapeDelay mono, stereo;
    for (auto* d : { &mono, &stereo })
    {
        d->prepare(kSampleRate, 1.0f);
        d->setDelay(0.05f);
        d->setFeedback(0.6f);
        d->setMix(0.5f);
        d->setWowDepth(0.002f);
        d->setFlutterDepth(0.0005f);
        d->setDiffusion(0.5f);
    }
    for (int i = 0; i < 20000; ++i)
    {
        const float x = 0.5f * std::sin(0.03f * static_cast<float>(i));
        REQUIRE(mono.process(x) == stereo.processStereo(x).first);
    }
}

TEST_CASE("TapeDelay: stereo PingPong alternates echoes L, R, L", "[tapedelay]")
{
    // Mono input enters the left loop; each loop feeds the other.  Echo k
    // (at k·D) lands in L for odd k and in R for even k.  The silent side
    // must stay 60 dB below the active side: all that crosses is the 20 Hz
    // high-pass's ring from the previous echo (τ = 1/(ζ·ω₀) ≈ 11 ms, so
    // ≈ e⁻⁹ in amplitude after D = 100 ms).
    ideath::TapeDelay d;
    cleanDelay(d, 0.1f, 0.5f);
    d.setStereoMode(ideath::TapeDelay::StereoMode::PingPong);
    const int D = 4410;
    std::vector<float> l(5 * D), r(5 * D);
    for (int i = 0; i < 5 * D; ++i)
    {
        auto [a, b] = d.processStereo(i == 0 ? 1.0f : 0.0f);
        l[i] = a;
        r[i] = b;
    }
    auto energy = [](const std::vector<float>& x, int c) {
        double e = 0.0;
        for (int i = c - 100; i <= c + 100; ++i)
            e += static_cast<double>(x[i]) * x[i];
        return e;
    };
    for (int k = 1; k <= 4; ++k)
    {
        const double el = energy(l, k * D), er = energy(r, k * D);
        INFO("echo " << k << ": L " << el << " R " << er);
        if (k % 2 == 1)
            REQUIRE(er < el * 1e-6);
        else
            REQUIRE(el < er * 1e-6);
    }
}

TEST_CASE("TapeDelay: wow/flutter run in quadrature on the right channel", "[tapedelay]")
{
    // With offset 0 and no modulation both channels read the same loop
    // point and are identical; with wow on they must differ.
    auto render = [](float wow) {
        ideath::TapeDelay d;
        d.prepare(kSampleRate, 1.0f);
        d.setDelay(0.05f);
        d.setFeedback(0.3f);
        d.setMix(1.0f);
        d.setWowDepth(wow);
        d.setFlutterDepth(0.0f);
        d.setStereoOffset(0.0f);
        double diff = 0.0;
        for (int i = 0; i < 44100; ++i)
        {
            auto [l, r] = d.processStereo(0.5f * std::sin(0.05f * static_cast<float>(i)));
            diff += std::fabs(l - r);
        }
        return diff;
    };
    REQUIRE(render(0.0f) == 0.0);
    REQUIRE(render(0.002f) > 0.0);
}

TEST_CASE("TapeDelay: saturation off keeps repeats linear", "[tapedelay]")
{
    // Below the 0.8 knee the clean path is the identity, so halving the
    // input halves every output sample (up to one float rounding step,
    // 2⁻²³ relative).  With saturation on the same check fails (tanh).
    auto maxRelErr = [](bool sat) {
        ideath::TapeDelay a, b;
        for (auto* d : { &a, &b })
        {
            d->prepare(kSampleRate, 1.0f);
            d->setDelay(0.01f);
            d->setFeedback(0.7f);
            d->setMix(1.0f);
            d->setDrive(1.0f);
            d->setSaturation(sat);
        }
        double err = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            const float x = 0.3f * std::sin(0.02f * static_cast<float>(i));
            const float ya = a.process(x);
            const float yb = b.process(0.5f * x);
            if (std::fabs(ya) > 1e-3f)
                err = std::max(err, static_cast<double>(std::fabs(yb - 0.5f * ya) / std::fabs(ya)));
        }
        return err;
    };
    REQUIRE(maxRelErr(false) < 1e-5);
    REQUIRE(maxRelErr(true) > 1e-3);
}

TEST_CASE("TapeDelay: clean repeats stay bright at a fractional delay", "[tapedelay]")
{
    // Delay of 441.5 samples (worst-case fraction 0.5).  The 8 kHz content
    // of the 4th echo, corrected for fb³, must be within 3 dB (half power)
    // of the 1st echo's.  Linear interpolation loses 1.5 dB per pass at
    // 8 kHz (|cos(πf/sr)|) → −4.5 dB after three passes; the allpass
    // interpolator has unit magnitude (0 dB per pass; only the wide-open
    // tone filters remain).
    ideath::TapeDelay d;
    cleanDelay(d, 441.5f / kSampleRate, 0.8f);
    const int D = 442;
    std::vector<float> y(5 * D);
    for (int i = 0; i < 5 * D; ++i)
        y[i] = d.process(i == 0 ? 1.0f : 0.0f);
    auto power8k = [&](int k) {
        // Goertzel over the 128 samples centred on echo k.
        const double w = 2.0 * 3.14159265358979 * 8000.0 / kSampleRate;
        const double c = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = k * D - 64; i < k * D + 64; ++i)
        {
            const double s0 = y[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - c * s1 * s2;
    };
    const double fb3 = 0.8 * 0.8 * 0.8;
    const double lossDb = 10.0 * std::log10(power8k(4) / (power8k(1) * fb3 * fb3));
    INFO("8 kHz loss over three passes: " << lossDb << " dB");
    REQUIRE(lossDb >= -3.0);
}

TEST_CASE("TapeDelay: shortest delay is 2 samples", "[tapedelay]")
{
    ideath::TapeDelay d;
    cleanDelay(d, 0.0f, 0.0f);
    std::vector<float> y(16);
    for (int i = 0; i < 16; ++i)
        y[i] = d.process(i == 0 ? 1.0f : 0.0f);
    REQUIRE(argmaxAbs(y) == 2);
}

TEST_CASE("TapeDelay: diffusion smears each echo without moving or losing it", "[tapedelay]")
{
    // D = 0.2 s, fb 0.7.  Echo windows span ±D/2 (the diffuser at g = 0.7
    // rings ≈ 19 × its longest stage, ≈ 80 ms, well inside D/2).
    auto render = [](float diffusion) {
        ideath::TapeDelay d;
        cleanDelay(d, 0.2f, 0.7f);
        d.setDiffusion(diffusion);
        std::vector<float> y(3 * 8820 + 4410);
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = d.process(i == 0 ? 1.0f : 0.0f);
        return y;
    };
    const int D = 8820;
    auto energy = [](const std::vector<float>& y, int from, int to) {
        double e = 0.0;
        for (int i = from; i < to; ++i)
            e += static_cast<double>(y[i]) * y[i];
        return e;
    };
    auto dry = render(0.0f);
    auto wet = render(1.0f);

    // Diffusion 0: the first echo is a single click (≥ 99 % of its energy
    // within ±5 samples).  Diffusion 1: spread out (< 50 % there).
    const double dryEcho = energy(dry, D - D / 2, D + D / 2);
    const double wetEcho = energy(wet, D - D / 2, D + D / 2);
    REQUIRE(energy(dry, D - 5, D + 6) >= 0.99 * dryEcho);
    REQUIRE(energy(wet, D - 5, D + 6) < 0.5 * wetEcho);

    // Not moved: an allpass's mean group delay is its length for any g,
    // and that length is taken out of the read, so the smeared echo's
    // energy centroid sits at D.  ±1 ms is below the ≈ 2 ms temporal-order
    // threshold for clicks.
    double moment = 0.0;
    for (int i = D - D / 2; i < D + D / 2; ++i)
        moment += static_cast<double>(i) * wet[static_cast<size_t>(i)] * wet[static_cast<size_t>(i)];
    const double centroid = moment / wetEcho;
    INFO("echo centroid " << centroid << " samples, D = " << D);
    REQUIRE(std::fabs(centroid - D) <= 0.001 * kSampleRate);

    // Not lost: allpasses are lossless, so the echo-to-echo energy ratio
    // matches the undiffused loop within the 1 dB level JND.
    const double dryRatio = energy(dry, 2 * D - D / 2, 2 * D + D / 2) / dryEcho;
    const double wetRatio = energy(wet, 2 * D - D / 2, 2 * D + D / 2) / wetEcho;
    INFO("echo2/echo1: dry " << dryRatio << " wet " << wetRatio);
    REQUIRE(std::fabs(10.0 * std::log10(wetRatio / dryRatio)) <= 1.0);
}

TEST_CASE("TapeDelay: stereo 10 s stability, clean repeats at max feedback", "[tapedelay][stability]")
{
    // Saturation off keeps |out| ≤ 1 through the soft ceiling.
    for (auto mode : { ideath::TapeDelay::StereoMode::Offset, ideath::TapeDelay::StereoMode::PingPong })
    {
        ideath::TapeDelay d;
        d.prepare(kSampleRate, 1.0f);
        d.setDelay(0.13f);
        d.setFeedback(0.999f);
        d.setMix(1.0f);
        d.setSaturation(false);
        d.setDiffusion(1.0f);
        d.setStereoMode(mode);
        d.setStereoOffset(0.05f);
        d.setWowDepth(0.0003f);
        for (int i = 0; i < 441000; ++i)
        {
            auto [l, r] = d.processStereo(0.8f * std::sin(0.01f * static_cast<float>(i)));
            REQUIRE(std::isfinite(l));
            REQUIRE(std::fabs(l) <= 1.0f + 1e-5f);
            REQUIRE(std::fabs(r) <= 1.0f + 1e-5f);
        }
    }
}

TEST_CASE("TapeDelay: sweeping across the diffuser threshold does not click", "[tapedelay]")
{
    // The diffuser is bypassed below ≈ 10.4 ms (its own length).  Sweep
    // the delay 9.5 → 11.5 → 9.5 ms over 2 s on a 200 Hz sine (0.3, fb
    // 0.5, mix 1), clean repeats with the tone filters wide open so the
    // loop gain is the feedback alone.  The wet signal is then bounded by
    // 0.3 / (1 − 0.5) = 0.6, so its steepest sample step is
    // 0.6 · 2π · 200 / 44 100 = 0.0171; the 0.4 %/s delay sweep adds no
    // meaningful Doppler.  A jump of the read point (≈ 10 ms) shows up as a
    // step far above that (0.38 before the crossfade); allow twice the
    // signal's own steepest step.
    for (float diffusion : { 0.0f, 0.7f })
    {
        ideath::TapeDelay d;
        d.prepare(kSampleRate, 1.0f);
        d.setFeedback(0.5f);
        d.setMix(1.0f);
        d.setDiffusion(diffusion);
        d.setSaturation(false);
        d.setLowpass(20000.0f);
        d.setHighpass(5.0f);
        d.setDelay(0.0095f);
        float prev = 0.0f, maxStep = 0.0f;
        const int n = static_cast<int>(2.0f * kSampleRate);
        for (int i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(n); // 0 … 1
            const float tri = t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t;      // 0 → 1 → 0
            d.setDelay(0.0095f + 0.002f * tri);
            const float y = d.process(0.3f * std::sin(2.0f * 3.14159265f * 200.0f
                                                      * static_cast<float>(i) / kSampleRate));
            if (i > static_cast<int>(0.05f * kSampleRate))
                maxStep = std::max(maxStep, std::fabs(y - prev));
            prev = y;
        }
        INFO("diffusion " << diffusion << ": largest step " << maxStep);
        REQUIRE(maxStep <= 2.0f * 0.0171f);
    }
}

TEST_CASE("TapeDelay: switching diffusion on and off mid-stream is seamless", "[tapedelay]")
{
    // The diffuser only runs while diffusion > 0.  Switching it on or off
    // with repeats in flight must neither click nor drop out.  Clean 200 Hz
    // sine (0.3, fb 0.5, delay 100 ms, filters wide open): the wet signal is
    // bounded by 0.6, so its steepest sample step is 0.0171 (see the
    // threshold-sweep test) — allow twice that.  A dropout shows as a 10 ms
    // window far below the steady level; the allpass chain is lossless, so
    // no window may fall below half the steady RMS (−6 dB, half amplitude).
    ideath::TapeDelay d;
    d.prepare(kSampleRate, 1.0f);
    d.setDelay(0.1f);
    d.setFeedback(0.5f);
    d.setMix(1.0f);
    d.setSaturation(false);
    d.setLowpass(20000.0f);
    d.setHighpass(5.0f);
    const int n = static_cast<int>(4.0f * kSampleRate);
    const int win = static_cast<int>(0.01f * kSampleRate);
    std::vector<float> y(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        if (i == static_cast<int>(1.0f * kSampleRate))
            d.setDiffusion(0.7f);
        if (i == static_cast<int>(2.5f * kSampleRate))
            d.setDiffusion(0.0f);
        y[static_cast<size_t>(i)] = d.process(0.3f * std::sin(2.0f * 3.14159265f * 200.0f
                                                              * static_cast<float>(i) / kSampleRate));
    }
    const int from = static_cast<int>(0.8f * kSampleRate); // after the build-up
    double steady = 0.0;
    for (int i = from - win * 10; i < from; ++i)
        steady += static_cast<double>(y[static_cast<size_t>(i)]) * y[static_cast<size_t>(i)];
    steady = std::sqrt(steady / (win * 10));
    float maxStep = 0.0f;
    double minRms = 1e9;
    for (int i = from; i + win <= n; i += win)
    {
        double e = 0.0;
        for (int j = i; j < i + win; ++j)
        {
            e += static_cast<double>(y[static_cast<size_t>(j)]) * y[static_cast<size_t>(j)];
            maxStep = std::max(maxStep, std::fabs(y[static_cast<size_t>(j)] - y[static_cast<size_t>(j - 1)]));
        }
        minRms = std::min(minRms, std::sqrt(e / win));
    }
    INFO("largest step " << maxStep << ", lowest 10 ms RMS " << minRms << " vs steady " << steady);
    REQUIRE(maxStep <= 2.0f * 0.0171f);
    REQUIRE(minRms >= 0.5 * steady);
}
