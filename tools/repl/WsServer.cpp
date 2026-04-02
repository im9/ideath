#include "WsServer.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>

// For SHA-1 (WebSocket handshake)
#include <CommonCrypto/CommonDigest.h>

namespace ideath { namespace repl {

// --- Base64 encode ---
static std::string base64Encode(const uint8_t* data, size_t len)
{
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? table[n & 0x3F] : '=';
    }
    return out;
}

WsServer::WsServer(int port)
    : port_(port)
{
}

WsServer::~WsServer()
{
    stop();
}

void WsServer::start()
{
    running_ = true;
    acceptThread_ = std::thread(&WsServer::acceptLoop, this);
    sendThread_ = std::thread(&WsServer::sendLoop, this);
}

void WsServer::stop()
{
    running_ = false;

    if (listenFd_ >= 0)
    {
        ::close(listenFd_);
        listenFd_ = -1;
    }

    if (acceptThread_.joinable())
        acceptThread_.join();
    if (sendThread_.joinable())
        sendThread_.join();

    // Close all client connections
    std::lock_guard<std::mutex> lock(clientMutex_);
    for (int fd : clients_)
        ::close(fd);
    clients_.clear();
    clientCount_ = 0;
}

void WsServer::pushSample(float sample)
{
    size_t wp = ringWrite_.load(std::memory_order_relaxed);
    ring_[wp % kRingCapacity] = sample;
    ringWrite_.store(wp + 1, std::memory_order_release);
}

void WsServer::acceptLoop()
{
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
    {
        std::cerr << "[ws] Failed to create socket." << std::endl;
        return;
    }

    int opt = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        if (running_)
            std::cerr << "[ws] Failed to bind port " << port_ << ": "
                      << std::strerror(errno) << std::endl;
        ::close(listenFd_);
        listenFd_ = -1;
        return;
    }

    if (::listen(listenFd_, 4) < 0)
    {
        if (running_)
            std::cerr << "[ws] Failed to listen: " << std::strerror(errno) << std::endl;
        ::close(listenFd_);
        listenFd_ = -1;
        return;
    }

    while (running_)
    {
        int clientFd = ::accept(listenFd_, nullptr, nullptr);
        if (clientFd < 0)
            break;

        if (!doHandshake(clientFd))
        {
            ::close(clientFd);
            continue;
        }

        // Disable Nagle for low latency
        int flag = 1;
        ::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        // Set non-blocking for sends
        int flags = ::fcntl(clientFd, F_GETFL, 0);
        ::fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

        {
            std::lock_guard<std::mutex> lock(clientMutex_);
            clients_.push_back(clientFd);
            clientCount_ = static_cast<int>(clients_.size());
        }
    }
}

void WsServer::sendLoop()
{
    // Send buffer: batch samples into chunks
    // Protocol: binary frame containing:
    //   4 bytes: float32 sampleRate (little-endian)
    //   N * 4 bytes: float32 samples (little-endian)
    constexpr size_t kBatchSize = 1024; // samples per frame (~23ms at 44100)
    constexpr int kSendIntervalMs = 20;

    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(kSendIntervalMs));

        size_t wp = ringWrite_.load(std::memory_order_acquire);
        size_t available = wp - ringRead_;
        if (available == 0)
            continue;

        // Cap to batch size, handle overflow
        if (available > kRingCapacity)
        {
            ringRead_ = wp - kBatchSize;
            available = kBatchSize;
        }
        if (available > kBatchSize)
        {
            // Skip old samples, send most recent batch
            ringRead_ = wp - kBatchSize;
            available = kBatchSize;
        }

        // Build binary payload: sampleRate (4 bytes) + samples
        float sr = sampleRate_.load(std::memory_order_relaxed);
        size_t payloadSize = 4 + available * 4;
        std::vector<uint8_t> payload(payloadSize);

        std::memcpy(payload.data(), &sr, 4);
        for (size_t i = 0; i < available; ++i)
        {
            float s = ring_[(ringRead_ + i) % kRingCapacity];
            std::memcpy(payload.data() + 4 + i * 4, &s, 4);
        }
        ringRead_ = wp;

        // Broadcast to all clients
        std::lock_guard<std::mutex> lock(clientMutex_);
        auto it = clients_.begin();
        while (it != clients_.end())
        {
            if (!sendBinaryFrame(*it, payload.data(), payloadSize))
            {
                ::close(*it);
                it = clients_.erase(it);
                clientCount_ = static_cast<int>(clients_.size());
            }
            else
            {
                ++it;
            }
        }
    }
}

bool WsServer::doHandshake(int fd)
{
    // Read HTTP request
    char buf[2048];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return false;
    buf[n] = '\0';

    std::string request(buf);

    // Reject non-WebSocket requests (must be GET with Upgrade header)
    if (request.find("Upgrade: websocket") == std::string::npos
        && request.find("Upgrade: WebSocket") == std::string::npos)
        return false;

    // Origin check: only allow local origins (file://, localhost, 127.0.0.1)
    auto originPos = request.find("Origin: ");
    if (originPos != std::string::npos)
    {
        auto originEnd = request.find("\r\n", originPos);
        std::string origin = request.substr(originPos + 8, originEnd - originPos - 8);
        bool allowed = origin == "null"  // file:// origin in some browsers
                    || origin.find("file://") == 0
                    || origin.find("http://localhost") == 0
                    || origin.find("http://127.0.0.1") == 0
                    || origin.find("https://localhost") == 0
                    || origin.find("https://127.0.0.1") == 0;
        if (!allowed)
        {
            std::cerr << "[ws] Rejected connection from origin: " << origin << std::endl;
            return false;
        }
    }

    // Find Sec-WebSocket-Key header
    std::string keyHeader = "Sec-WebSocket-Key: ";
    auto pos = request.find(keyHeader);
    if (pos == std::string::npos) return false;

    auto keyStart = pos + keyHeader.size();
    auto keyEnd = request.find("\r\n", keyStart);
    if (keyEnd == std::string::npos) return false;

    std::string key = request.substr(keyStart, keyEnd - keyStart);

    // Compute accept hash: SHA1(key + magic) → base64
    std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concat = key + magic;

    uint8_t hash[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1(concat.c_str(), static_cast<CC_LONG>(concat.size()), hash);

    std::string accept = base64Encode(hash, CC_SHA1_DIGEST_LENGTH);

    // Send response
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n";

    ssize_t sent = ::write(fd, response.c_str(), response.size());
    return sent == static_cast<ssize_t>(response.size());
}

bool WsServer::sendBinaryFrame(int fd, const uint8_t* data, size_t len)
{
    // WebSocket binary frame: opcode 0x82
    uint8_t header[10];
    size_t headerLen = 0;

    header[0] = 0x82; // FIN + binary opcode
    headerLen = 1;

    if (len < 126)
    {
        header[1] = static_cast<uint8_t>(len);
        headerLen = 2;
    }
    else if (len < 65536)
    {
        header[1] = 126;
        header[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
        header[3] = static_cast<uint8_t>(len & 0xFF);
        headerLen = 4;
    }
    else
    {
        header[1] = 127;
        for (int i = 0; i < 8; ++i)
            header[2 + i] = static_cast<uint8_t>((len >> (56 - 8 * i)) & 0xFF);
        headerLen = 10;
    }

    // Try to send header + data
    ssize_t s1 = ::send(fd, header, headerLen, MSG_NOSIGNAL);
    if (s1 <= 0) return false;

    ssize_t s2 = ::send(fd, data, len, MSG_NOSIGNAL);
    if (s2 <= 0) return false;

    return true;
}

}} // namespace ideath::repl
