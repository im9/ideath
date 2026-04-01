#include "TcpServer.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace ideath { namespace repl {

TcpServer::TcpServer(int port)
    : m_port(port)
{
}

TcpServer::~TcpServer()
{
    stop();
}

void TcpServer::start(CommandHandler handler)
{
    m_handler = std::move(handler);
    m_running = true;
    m_thread = std::thread(&TcpServer::run, this);
}

void TcpServer::stop()
{
    m_running = false;

    // Close the listening socket to unblock accept()
    if (m_listenFd >= 0)
    {
        ::close(m_listenFd);
        m_listenFd = -1;
    }

    if (m_thread.joinable())
        m_thread.join();
}

void TcpServer::run()
{
    m_listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0)
    {
        std::cerr << "[tcp] Failed to create socket." << std::endl;
        return;
    }

    int opt = 1;
    ::setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 only
    addr.sin_port = htons(static_cast<uint16_t>(m_port));

    if (::bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        if (m_running)
            std::cerr << "[tcp] Failed to bind port " << m_port << ": " << std::strerror(errno) << std::endl;
        ::close(m_listenFd);
        m_listenFd = -1;
        return;
    }

    if (::listen(m_listenFd, 4) < 0)
    {
        if (m_running)
            std::cerr << "[tcp] Failed to listen: " << std::strerror(errno) << std::endl;
        ::close(m_listenFd);
        m_listenFd = -1;
        return;
    }

    std::cout << "[tcp] Listening on 127.0.0.1:" << m_port << std::endl;

    while (m_running)
    {
        int clientFd = ::accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0)
            break; // socket closed or error

        // Read all data from this connection
        std::string buf;
        char chunk[1024];
        ssize_t n;
        while ((n = ::read(clientFd, chunk, sizeof(chunk))) > 0)
            buf.append(chunk, static_cast<size_t>(n));

        ::close(clientFd);

        // Process each line as a separate command
        std::istringstream stream(buf);
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.empty())
                continue;
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            m_handler(line);
        }
    }
}

}} // namespace ideath::repl
