#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio/miniaudio.h"

#include "TrackManager.h"
#include "CommandParser.h"
#include "SharedState.h"
#include "TcpServer.h"
#include "WsServer.h"
#include "SpectrumRenderer.h"

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <thread>
#include <chrono>

static ideath::repl::TrackManager g_tracks;
static ideath::repl::WsServer g_wsServer(7778);
static int g_activeTrack = 0; // command-thread only, protected by cmdMutex

// --- Scope / Spectrum auto-refresh ---
static std::atomic<bool> g_scopeAutoRunning{false};
static std::thread g_scopeThread;
static std::atomic<bool> g_spectrumAutoRunning{false};
static std::thread g_spectrumThread;

static void scopeAutoLoop()
{
    auto& scope = g_tracks.getScope();
    scope.setEnabled(true);

    // Let the buffer fill before first render
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    constexpr size_t kSamples = 1024;
    constexpr int kWidth = 72;
    constexpr int kHeight = 14;
    // ANSI: lines to move up = kHeight + 3 (top border + rows + bottom border + stats)
    constexpr int kTotalLines = kHeight + 3;

    bool firstDraw = true;

    while (g_scopeAutoRunning.load(std::memory_order_relaxed))
    {
        float buf[kSamples];
        size_t n = scope.snapshot(buf, kSamples);

        float gr = g_tracks.getLimiter().getGainReductionDb();
        std::string rendered = ideath::repl::ScopeBuffer::render(buf, n, kWidth, kHeight, gr);

        // Move cursor up to overwrite previous frame (except first)
        if (!firstDraw)
            std::cout << "\033[" << kTotalLines << "A";
        std::cout << rendered << std::flush;
        firstDraw = false;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

static void scopeAutoStart()
{
    if (g_scopeAutoRunning.load(std::memory_order_relaxed))
        return;
    g_scopeAutoRunning.store(true, std::memory_order_relaxed);
    g_scopeThread = std::thread(scopeAutoLoop);
}

static void scopeAutoStop()
{
    if (!g_scopeAutoRunning.load(std::memory_order_relaxed))
        return;
    g_scopeAutoRunning.store(false, std::memory_order_relaxed);
    if (g_scopeThread.joinable())
        g_scopeThread.join();
    g_tracks.getScope().setEnabled(false);
}

// --- Spectrum auto-refresh ---
static void spectrumAutoLoop()
{
    auto& scope = g_tracks.getScope();
    scope.setEnabled(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    constexpr size_t kSamples = ideath::repl::SpectrumRenderer::kFFTSize;
    constexpr int kWidth = 72;
    constexpr int kHeight = 14;
    constexpr int kTotalLines = ideath::repl::SpectrumRenderer::totalLines(kHeight);

    bool firstDraw = true;

    while (g_spectrumAutoRunning.load(std::memory_order_relaxed))
    {
        float buf[kSamples];
        size_t n = scope.snapshot(buf, kSamples);

        std::string rendered = ideath::repl::SpectrumRenderer::render(
            buf, n, 44100.0f, kWidth, kHeight);

        if (!firstDraw)
            std::cout << "\033[" << kTotalLines << "A";
        std::cout << rendered << std::flush;
        firstDraw = false;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

static void spectrumAutoStart()
{
    if (g_spectrumAutoRunning.load(std::memory_order_relaxed))
        return;
    g_spectrumAutoRunning.store(true, std::memory_order_relaxed);
    g_spectrumThread = std::thread(spectrumAutoLoop);
}

static void spectrumAutoStop()
{
    if (!g_spectrumAutoRunning.load(std::memory_order_relaxed))
        return;
    g_spectrumAutoRunning.store(false, std::memory_order_relaxed);
    if (g_spectrumThread.joinable())
        g_spectrumThread.join();
    // Only disable scope if scope auto isn't also running
    if (!g_scopeAutoRunning.load(std::memory_order_relaxed))
        g_tracks.getScope().setEnabled(false);
}

static void audioCallback(ma_device* /*device*/, void* output, const void* /*input*/, ma_uint32 frameCount)
{
    g_tracks.applyPendingState();

    auto* out = static_cast<float*>(output);
    for (ma_uint32 i = 0; i < frameCount; ++i)
    {
        float s = g_tracks.process();
        out[i] = s;
        g_wsServer.pushSample(s);
    }
}

static float parseFloat(const std::string& s, float fallback)
{
    try { return std::stof(s); }
    catch (...) { return fallback; }
}

static void printTrackStatus()
{
    using namespace ideath::repl;
    for (int i = 0; i < kMaxTracks; ++i)
    {
        auto& mix = g_tracks.getMix(i);
        bool m = mix.muted.load(std::memory_order_relaxed);
        bool s = mix.solo.load(std::memory_order_relaxed);
        float v = mix.volume.load(std::memory_order_relaxed);

        // Only show tracks that are active or modified
        auto& sh = g_tracks.getShared(i);
        bool hasSource = sh.staging.source != SourceType::None;
        bool hasSeq = sh.seqStaging.running;
        if (!hasSource && !hasSeq && !m && !s && i != g_activeTrack)
            continue;

        std::cout << "  [" << (i + 1) << "]"
                  << (i == g_activeTrack ? "*" : " ")
                  << " vol=" << v
                  << (m ? " MUTE" : "")
                  << (s ? " SOLO" : "")
                  << (hasSeq ? " seq" : "")
                  << std::endl;
    }
}

/// Handle track command. Returns true if handled.
static bool handleTrackCommand(const std::string& line)
{
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    if (cmd != "track") return false;

    std::string arg1;
    if (!(iss >> arg1))
    {
        // Just "track" — show status
        printTrackStatus();
        return true;
    }

    // Parse track number (1-indexed)
    int trackNum = 0;
    try { trackNum = std::stoi(arg1); }
    catch (...) { trackNum = 0; }

    if (trackNum < 1 || trackNum > ideath::repl::kMaxTracks)
    {
        std::cout << "Track number must be 1-" << ideath::repl::kMaxTracks << std::endl;
        return true;
    }

    int trackIdx = trackNum - 1;

    std::string subcmd;
    if (!(iss >> subcmd))
    {
        // "track N" — switch active track
        g_activeTrack = trackIdx;
        std::cout << "Active track: " << trackNum << std::endl;
        return true;
    }

    if (subcmd == "mute")
    {
        auto& mix = g_tracks.getMix(trackIdx);
        bool curr = mix.muted.load(std::memory_order_relaxed);
        mix.muted.store(!curr, std::memory_order_relaxed);
        std::cout << "Track " << trackNum << (curr ? " unmuted" : " muted") << std::endl;
    }
    else if (subcmd == "solo")
    {
        auto& mix = g_tracks.getMix(trackIdx);
        bool curr = mix.solo.load(std::memory_order_relaxed);
        mix.solo.store(!curr, std::memory_order_relaxed);
        std::cout << "Track " << trackNum << (curr ? " unsolo" : " solo") << std::endl;
    }
    else if (subcmd == "vol")
    {
        std::string vstr;
        if (iss >> vstr)
        {
            float v = parseFloat(vstr, 1.0f);
            g_tracks.getMix(trackIdx).volume.store(v, std::memory_order_relaxed);
            std::cout << "Track " << trackNum << " volume: " << v << std::endl;
        }
    }
    else
    {
        std::cout << "Unknown track subcommand: " << subcmd << std::endl;
    }
    return true;
}

/// Handle limiter command. Returns true if handled.
static bool handleLimiterCommand(const std::string& line)
{
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    if (cmd != "limiter") return false;

    std::string arg;
    if (!(iss >> arg))
    {
        // Just "limiter" — show status
        bool on = g_tracks.limiterEnabled.load(std::memory_order_relaxed);
        float gr = g_tracks.getLimiter().getGainReductionDb();
        std::cout << "Limiter: " << (on ? "ON" : "OFF")
                  << "  GR: " << gr << " dB" << std::endl;
        return true;
    }

    if (arg == "off")
    {
        g_tracks.limiterEnabled.store(false, std::memory_order_relaxed);
        std::cout << "Limiter OFF" << std::endl;
    }
    else if (arg == "on")
    {
        g_tracks.limiterEnabled.store(true, std::memory_order_relaxed);
        std::cout << "Limiter ON" << std::endl;
    }
    else
    {
        // Treat as threshold in dB
        float dB = parseFloat(arg, 0.0f);
        g_tracks.getLimiter().setThreshold(dB);
        g_tracks.limiterEnabled.store(true, std::memory_order_relaxed);
        std::cout << "Limiter threshold: " << dB << " dB" << std::endl;
    }
    return true;
}

/// Handle scope command. Returns true if handled.
static bool handleScopeCommand(const std::string& line)
{
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    if (cmd != "scope") return false;

    std::string arg;
    if (iss >> arg)
    {
        if (arg == "off")
        {
            scopeAutoStop();
            std::cout << "Scope OFF" << std::endl;
            return true;
        }
        if (arg == "on")
        {
            std::cout << "Scope ON (auto-refresh, 'scope off' to stop)" << std::endl;
            scopeAutoStart();
            return true;
        }
    }

    // No argument — one-shot snapshot
    auto& scope = g_tracks.getScope();
    scope.setEnabled(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    constexpr size_t kSamples = 1024;
    float buf[kSamples];
    size_t n = scope.snapshot(buf, kSamples);

    float gr = g_tracks.getLimiter().getGainReductionDb();
    std::cout << ideath::repl::ScopeBuffer::render(buf, n, 72, 14, gr);

    // Disable capture if auto mode isn't running
    if (!g_scopeAutoRunning.load(std::memory_order_relaxed))
        scope.setEnabled(false);

    return true;
}

/// Handle spectrum command. Returns true if handled.
static bool handleSpectrumCommand(const std::string& line)
{
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    if (cmd != "spectrum") return false;

    std::string arg;
    if (iss >> arg)
    {
        if (arg == "off")
        {
            spectrumAutoStop();
            std::cout << "Spectrum OFF" << std::endl;
            return true;
        }
        if (arg == "on")
        {
            std::cout << "Spectrum ON (auto-refresh, 'spectrum off' to stop)" << std::endl;
            spectrumAutoStart();
            return true;
        }
    }

    // No argument — one-shot snapshot
    auto& scope = g_tracks.getScope();
    scope.setEnabled(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    constexpr size_t kSamples = ideath::repl::SpectrumRenderer::kFFTSize;
    float buf[kSamples];
    size_t n = scope.snapshot(buf, kSamples);

    std::cout << ideath::repl::SpectrumRenderer::render(buf, n, 44100.0f, 72, 14);

    if (!g_scopeAutoRunning.load(std::memory_order_relaxed)
        && !g_spectrumAutoRunning.load(std::memory_order_relaxed))
        scope.setEnabled(false);

    return true;
}

static void processLine(const std::string& line)
{
    if (handleTrackCommand(line))
        return;
    if (handleLimiterCommand(line))
        return;
    if (handleScopeCommand(line))
        return;
    if (handleSpectrumCommand(line))
        return;
    ideath::repl::parseCommand(line, g_tracks.getShared(g_activeTrack));
}

static std::string prompt()
{
    return "ideath[" + std::to_string(g_activeTrack + 1) + "]> ";
}

int main()
{
    constexpr float kSampleRate = 44100.0f;

    g_tracks.prepare(kSampleRate);

    // --- WebSocket server for viewer ---
    g_wsServer.setSampleRate(kSampleRate);
    g_wsServer.start();

    // --- miniaudio setup ---
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate        = static_cast<ma_uint32>(kSampleRate);
    config.periodSizeInFrames = 1024;
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

    // --- TCP server for editor integration (Cmd+Enter) ---
    std::mutex cmdMutex;

    ideath::repl::TcpServer tcpServer(7777);
    tcpServer.start([&](const std::string& line) {
        std::lock_guard<std::mutex> lock(cmdMutex);
        std::cout << "[tcp] " << line << std::endl;
        processLine(line);
    });

    // --- REPL ---
    std::cout << "iDEATH REPL — type 'help' for commands, 'quit' to exit." << std::endl;
    std::cout << "       TCP listening on 127.0.0.1:7777" << std::endl;
    std::cout << "       WS  listening on 127.0.0.1:7778 (viewer)" << std::endl;
    std::cout << "       8 tracks available (track 1-8)" << std::endl;
    std::cout << prompt() << std::flush;

    std::string line;
    while (std::getline(std::cin, line))
    {
        std::lock_guard<std::mutex> lock(cmdMutex);
        if (line == "quit" || line == "exit")
        {
            scopeAutoStop();
            spectrumAutoStop();
            break;
        }
        processLine(line);
        std::cout << prompt() << std::flush;
    }

    tcpServer.stop();
    g_wsServer.stop();

    // --- Cleanup ---
    ma_device_uninit(&device);
    std::cout << "Bye." << std::endl;

    return 0;
}
