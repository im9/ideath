#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio/miniaudio.h"

#include "AudioEngine.h"
#include "CommandParser.h"
#include "SharedState.h"

#include <iostream>
#include <string>

static ideath::repl::AudioEngine g_engine;
static ideath::repl::SharedState g_shared;

static void audioCallback(ma_device* /*device*/, void* output, const void* /*input*/, ma_uint32 frameCount)
{
    g_engine.applyPendingState(g_shared);

    auto* out = static_cast<float*>(output);
    for (ma_uint32 i = 0; i < frameCount; ++i)
    {
        float sample = g_engine.process();
        out[i] = sample;
    }
}

int main()
{
    constexpr float kSampleRate = 44100.0f;

    g_engine.prepare(kSampleRate);

    // --- miniaudio setup ---
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate        = static_cast<ma_uint32>(kSampleRate);
    config.dataCallback      = audioCallback;

    ma_device device;
    if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS)
    {
        std::cerr << "Failed to initialize audio device." << std::endl;
        return 1;
    }

    if (ma_device_start(&device) != MA_SUCCESS)
    {
        std::cerr << "Failed to start audio device." << std::endl;
        ma_device_uninit(&device);
        return 1;
    }

    // --- REPL ---
    std::cout << "iDEATH REPL — type 'help' for commands, 'quit' to exit." << std::endl;
    std::cout << "ideath> " << std::flush;

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (!ideath::repl::parseCommand(line, g_shared))
            break;
        std::cout << "ideath> " << std::flush;
    }

    // --- Cleanup ---
    ma_device_uninit(&device);
    std::cout << "Bye." << std::endl;

    return 0;
}
