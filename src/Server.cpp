#include "Server.hpp"
#include "Logger.hpp"
#include "Protocol.hpp"

#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cerrno>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#endif

Server::Server(int port, int maxConnections, size_t threadPoolSize, const std::string& logFile)
    : m_port(port), m_maxConnections(maxConnections), m_threadPool(threadPoolSize), m_logFile(logFile) {
}

Server::~Server() {
    stop();
}

int Server::setNonBlocking(int fd) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

bool Server::init() {
    Logger::getInstance().init(m_logFile);
    Logger::getInstance().info("Initializing Epoll TCP Server on port " + std::to_string(m_port) + "...");

#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Logger::getInstance().error("WSAStartup failed");
        return false;
    }
#endif

    // 1. Create socket
    m_serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverFd < 0) {
        Logger::getInstance().error("Failed to create socket: " + std::string(strerror(errno)));
        return false;
    }

    // 2. Set socket options SO_REUSEADDR
    int opt = 1;
    if (setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
        Logger::getInstance().error("setsockopt SO_REUSEADDR failed: " + std::string(strerror(errno)));
        return false;
    }

    // 3. Set non-blocking
    if (setNonBlocking(m_serverFd) < 0) {
        Logger::getInstance().error("Failed to set server socket to non-blocking");
        return false;
    }

    // 4. Bind socket to port
    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<uint16_t>(m_port));

    if (bind(m_serverFd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        Logger::getInstance().error("Bind failed on port " + std::to_string(m_port) + ": " + std::string(strerror(errno)));
        return false;
    }

    // 5. Listen for incoming TCP connections
    if (listen(m_serverFd, m_maxConnections) < 0) {
        Logger::getInstance().error("Listen failed: " + std::string(strerror(errno)));
        return false;
    }

#if !defined(_WIN32)
    // 6. Create epoll instance
    m_epollFd = epoll_create1(0);
    if (m_epollFd < 0) {
        Logger::getInstance().error("epoll_create1 failed: " + std::string(strerror(errno)));
        return false;
    }

    // 7. Add server socket to epoll instance
    struct epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = m_serverFd;

    if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_serverFd, &ev) < 0) {
        Logger::getInstance().error("epoll_ctl add server socket failed: " + std::string(strerror(errno)));
        return false;
    }
#endif

    Logger::getInstance().info("Server successfully listening on port " + std::to_string(m_port) + " (Max connections: " + std::to_string(m_maxConnections) + ")");
    return true;
}

void Server::run() {
    m_running = true;

#if defined(_WIN32)
    Logger::getInstance().error("Epoll event loop requires Linux POSIX environment.");
    return;
#else
    struct epoll_event events[MAX_EVENTS];

    while (m_running) {
        int nfds = epoll_wait(m_epollFd, events, MAX_EVENTS, 250); // 250ms timeout for responsive shutdown
        if (nfds < 0) {
            if (errno == EINTR) continue; // Interrupted by signal
            Logger::getInstance().error("epoll_wait error: " + std::string(strerror(errno)));
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t evs = events[i].events;

            if (fd == m_serverFd) {
                // New incoming client connection
                acceptNewConnection();
            } else if (evs & (EPOLLHUP | EPOLLERR)) {
                // Client socket error or closed
                disconnectClient(fd, "Socket error/hangup");
            } else if (evs & EPOLLIN) {
                // Data available to read from client socket
                handleClientRead(fd);
            }
        }
    }
#endif
}

void Server::acceptNewConnection() {
    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(m_serverFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);

        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // All incoming connections accepted
                break;
            }
            Logger::getInstance().error("Accept failed: " + std::string(strerror(errno)));
            break;
        }

        // Check connection limit
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            if (static_cast<int>(m_clientsByFd.size()) >= m_maxConnections) {
                Logger::getInstance().warn("Max connection threshold reached. Rejecting connection.");
                close(clientFd);
                break;
            }
        }

        // Set non-blocking socket
        if (setNonBlocking(clientFd) < 0) {
            Logger::getInstance().error("Failed to set client socket non-blocking");
            close(clientFd);
            continue;
        }

#if !defined(_WIN32)
        // Add client socket to epoll interest list
        struct epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLET; // Edge-triggered read
        ev.data.fd = clientFd;

        if (epoll_ctl(m_epollFd, EPOLL_CTL_ADD, clientFd, &ev) < 0) {
            Logger::getInstance().error("Failed to add client socket to epoll: " + std::string(strerror(errno)));
            close(clientFd);
            continue;
        }
#endif

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), ipStr, INET_ADDRSTRLEN);
        int clientPort = ntohs(clientAddr.sin_port);

        int clientId = m_nextClientId++;
        auto client = std::make_shared<ClientConnection>(clientId, clientFd, ipStr, clientPort);

        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            m_clientsByFd[clientFd] = client;
            m_clientsById[clientId] = client;
        }

        Logger::getInstance().info("New client connected from " + std::string(ipStr) + ":" + std::to_string(clientPort), clientId);

        // Send welcome banner
        std::string welcomeMsg = "[SERVER] Welcome! Your ID is " + std::to_string(clientId) + ". Default nick: " + client->getNickname() + ". Use /nick <name>, /list, /quit.";
        client->sendFrame(welcomeMsg);

        // Notify other clients
        broadcast("[SERVER] " + client->getNickname() + " joined the chat.", clientId);
    }
}

void Server::handleClientRead(int fd) {
    std::shared_ptr<ClientConnection> client;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clientsByFd.find(fd);
        if (it == m_clientsByFd.end()) return;
        client = it->second;
    }

    uint8_t buffer[READ_BUFFER_SIZE];
    bool socketClosed = false;

    // Edge-triggered read loop until EAGAIN/EWOULDBLOCK
    while (true) {
        ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            auto& readBuf = client->getReadBuffer();
            readBuf.insert(readBuf.end(), buffer, buffer + bytesRead);
        } else if (bytesRead == 0) {
            // Peer closed TCP connection gracefully
            socketClosed = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // All data read from kernel socket buffer
                break;
            } else {
                // Socket error
                socketClosed = true;
                break;
            }
        }
    }

    if (socketClosed) {
        disconnectClient(fd, "Client disconnected");
        return;
    }

    // Try decoding protocol frames from accumulated read buffer
    std::vector<std::string> messages;
    try {
        if (Protocol::decode(client->getReadBuffer(), messages)) {
            // For each complete frame decoded, dispatch task to thread pool for work decoupling
            for (const auto& msg : messages) {
                int clientId = client->getId();
                m_threadPool.enqueue([this, clientId, msg]() {
                    this->processClientMessage(clientId, msg);
                });
            }
        }
    } catch (const std::exception& e) {
        Logger::getInstance().error("Protocol decode error: " + std::string(e.what()), client->getId());
        disconnectClient(fd, "Protocol error");
    }
}

void Server::processClientMessage(int clientId, const std::string& message) {
    std::shared_ptr<ClientConnection> client;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clientsById.find(clientId);
        if (it == m_clientsById.end()) return;
        client = it->second;
    }

    Logger::getInstance().info("Received message: \"" + message + "\"", clientId);

    // Handle slash commands
    if (!message.empty() && message[0] == '/') {
        std::stringstream ss(message);
        std::string cmd;
        ss >> cmd;

        if (cmd == "/nick") {
            std::string newNick;
            ss >> newNick;
            if (!newNick.empty()) {
                std::string oldNick = client->getNickname();
                client->setNickname(newNick);
                std::string sysMsg = "[SERVER] " + oldNick + " changed nickname to " + newNick;
                Logger::getInstance().info(sysMsg, clientId);
                broadcast(sysMsg);
            } else {
                client->sendFrame("[SERVER] Usage: /nick <new_name>");
            }
        } else if (cmd == "/list") {
            std::stringstream listSs;
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            listSs << "[SERVER] Connected users (" << m_clientsById.size() << "): ";
            bool first = true;
            for (const auto& pair : m_clientsById) {
                if (!first) listSs << ", ";
                listSs << pair.second->getNickname() << " (#" << pair.first << ")";
                first = false;
            }
            client->sendFrame(listSs.str());
        } else if (cmd == "/quit") {
            client->sendFrame("[SERVER] Goodbye!");
            disconnectClient(client->getFd(), "User issued /quit");
        } else {
            client->sendFrame("[SERVER] Unknown command: " + cmd + ". Available: /nick, /list, /quit");
        }
    } else {
        // Normal chat broadcast
        std::string formattedMsg = "[" + client->getNickname() + "]: " + message;
        broadcast(formattedMsg, clientId);
    }
}

void Server::disconnectClient(int fd, const std::string& reason) {
    std::shared_ptr<ClientConnection> client;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clientsByFd.find(fd);
        if (it == m_clientsByFd.end()) return;
        client = it->second;

        m_clientsByFd.erase(it);
        m_clientsById.erase(client->getId());
    }

#if !defined(_WIN32)
    if (m_epollFd >= 0) {
        epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
    }
#endif

    Logger::getInstance().info("Client disconnected (" + reason + ")", client->getId());
    broadcast("[SERVER] " + client->getNickname() + " left the chat.");
}

void Server::broadcast(const std::string& message, int excludeClientId) {
    std::vector<std::shared_ptr<ClientConnection>> clients;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        clients.reserve(m_clientsById.size());
        for (const auto& pair : m_clientsById) {
            if (pair.first != excludeClientId) {
                clients.push_back(pair.second);
            }
        }
    }

    for (const auto& client : clients) {
        client->sendFrame(message);
    }
}

void Server::sendToClient(int clientId, const std::string& message) {
    std::shared_ptr<ClientConnection> client;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        auto it = m_clientsById.find(clientId);
        if (it != m_clientsById.end()) {
            client = it->second;
        }
    }
    if (client) {
        client->sendFrame(message);
    }
}

void Server::stop() {
    bool expected = true;
    if (m_running.compare_exchange_strong(expected, false)) {
        Logger::getInstance().info("Shutting down server...");

        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            for (const auto& pair : m_clientsByFd) {
#if !defined(_WIN32)
                if (m_epollFd >= 0) {
                    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, pair.first, nullptr);
                }
#endif
            }
            m_clientsByFd.clear();
            m_clientsById.clear();
        }

        m_threadPool.stop();

#if !defined(_WIN32)
        if (m_epollFd >= 0) {
            close(m_epollFd);
            m_epollFd = -1;
        }
        if (m_serverFd >= 0) {
            close(m_serverFd);
            m_serverFd = -1;
        }
#else
        if (m_serverFd >= 0) {
            closesocket(m_serverFd);
            m_serverFd = -1;
        }
        WSACleanup();
#endif

        Logger::getInstance().info("Server shutdown complete.");
    }
}
