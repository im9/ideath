#pragma once

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace ideath { namespace repl {

/// Lightweight WebSocket server for streaming audio data to a browser viewer.
/// Minimal RFC 6455 implementation — binary frames only, no extensions.
/// Audio thread pushes samples into a ring buffer; a dedicated send thread
/// drains the buffer and broadcasts to connected clients.
class WsServer
{
public:
    explicit WsServer(int port = 7778);
    ~WsServer();

    /// Start the accept thread and send thread.
    void start();

    /// Stop all threads and close connections.
    void stop();

    /// Push one sample from the audio thread (lock-free ring buffer).
    void pushSample(float sample);

    /// Set the sample rate (for metadata in the stream).
    void setSampleRate(float sr) { sampleRate_.store(sr, std::memory_order_relaxed); }

    /// Number of connected clients.
    int clientCount() const { return clientCount_.load(std::memory_order_relaxed); }

private:
    void acceptLoop();
    void sendLoop();

    bool doHandshake(int fd);
    bool sendBinaryFrame(int fd, const uint8_t* data, size_t len);

    int port_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<float> sampleRate_{44100.0f};
    std::atomic<int> clientCount_{0};

    // Accept thread
    std::thread acceptThread_;

    // Send thread
    std::thread sendThread_;

    // Connected client fds, protected by mutex
    std::mutex clientMutex_;
    std::vector<int> clients_;

    // Lock-free ring buffer for audio samples (audio thread → send thread)
    static constexpr size_t kRingCapacity = 8192;
    float ring_[kRingCapacity]{};
    std::atomic<size_t> ringWrite_{0};
    size_t ringRead_ = 0; // only accessed by send thread
};

}} // namespace ideath::repl
