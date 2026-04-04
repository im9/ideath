// Minimal ideath integration example.
// Generates 1 second of a filtered saw wave and writes raw float samples to stdout.
//
// Build:
//   cmake -B build -DCMAKE_BUILD_TYPE=Release
//   cmake --build build
//
// Run:
//   ./build/minimal > output.raw
//   # Import in Audacity: File → Import → Raw Data (32-bit float, mono, 48000 Hz)

#include <ideath/Oscillator.h>
#include <ideath/SVFilter.h>
#include <ideath/Envelope.h>
#include <ideath/Saturation.h>
#include <cstdio>

int main()
{
    constexpr float kSampleRate = 48000.0f;
    constexpr int kDurationSamples = static_cast<int>(kSampleRate); // 1 second

    // Set up a simple signal chain: Oscillator → SVFilter → Envelope → Saturation
    ideath::Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(110.0f); // A2

    ideath::SVFilter filter;
    filter.prepare(kSampleRate);
    filter.setCutoff(800.0f);
    filter.setResonance(0.6f);
    filter.setMode(ideath::SVFilter::Mode::Lowpass);

    ideath::AdsrEnvelope env;
    env.prepare(kSampleRate);
    env.setAttack(0.01f);
    env.setDecay(0.2f);
    env.setSustain(0.3f);
    env.setRelease(0.5f);

    // Trigger note
    env.noteOn();

    // Release after 0.5 seconds
    constexpr int kReleaseAt = static_cast<int>(kSampleRate * 0.5f);

    for (int i = 0; i < kDurationSamples; ++i)
    {
        if (i == kReleaseAt)
            env.noteOff();

        float sample = osc.process(1.0f);              // saw wave
        sample = filter.process(sample);                // lowpass filter
        sample *= env.process();                        // envelope
        sample = ideath::Saturation::tanhDrive(sample, 1.5f); // mild saturation

        std::fwrite(&sample, sizeof(float), 1, stdout);
    }

    return 0;
}
