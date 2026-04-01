#pragma once

#include "SharedState.h"

#include <atomic>
#include <thread>
#include <functional>

namespace ideath { namespace repl {

/// Lightweight TCP server that accepts connections and feeds lines
/// to a command handler. Designed for editor integration (Cmd+Enter workflow).
class TcpServer
{
public:
    using CommandHandler = std::function<void(const std::string&)>;

    explicit TcpServer(int port = 7777);
    ~TcpServer();

    /// Start listening in a background thread.
    void start(CommandHandler handler);

    /// Stop the server and join the thread.
    void stop();

private:
    void run();

    int m_port;
    int m_listenFd = -1;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    CommandHandler m_handler;
};

}} // namespace ideath::repl
