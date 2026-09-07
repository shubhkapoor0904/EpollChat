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
                break;
            }
            Logger::getInstance().error("Accept failed: " + std::string(strerror(errno)));
            break;
        }

        // Check connection limit with shared read lock first
        {
            std::shared_lock<std::shared_mutex> lock(m_clientsMutex);
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
            std::unique_lock<std::shared_mutex> lock(m_clientsMutex);
            m_clientsByFd[clientFd] = client;
            m_clientsById[clientId] = client;
        }

        Logger::getInstance().info("New client connected from " + std::string(ipStr) + ":" + std::to_string(clientPort), clientId);

        // Send welcome banner
        std::string welcomeMsg = "[SERVER] Welcome! Your ID is " + std::to_string(clientId) +
                                 ". Default nick: " + client->getNickname() +
                                 ". Channel: " + client->getChannel() +
                                 ". Type /help for commands.";
        client->sendFrame(welcomeMsg);

        // Notify other clients in channel
        broadcastToChannel(client->getChannel(), "[SERVER] " + client->getNickname() + " joined the chat.", clientId);

        // Send channel history
        sendChannelHistory(clientId, client->getChannel());
    }
}

void Server::handleClientRead(int fd) {
    std::shared_ptr<ClientConnection> client;
    {
        std::shared_lock<std::shared_mutex> lock(m_clientsMutex);
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
            socketClosed = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
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

void Server::storeChannelHistory(const std::string& channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    auto& history = m_channelHistory[channel];
    history.push_back(message);
    if (history.size() > MAX_HISTORY_PER_CHANNEL) {
        history.erase(history.begin());
    }
}

void Server::sendChannelHistory(int clientId, const std::string& channel) {
    std::vector<std::string> historyCopy;
    {
        std::lock_guard<std::mutex> lock(m_historyMutex);
        auto it = m_channelHistory.find(channel);
        if (it != m_channelHistory.end()) {
            historyCopy = it->second;
        }
    }

    if (!historyCopy.empty()) {
        sendToClient(clientId, "[SERVER] --- Recent Scrollback History for " + channel + " ---");
        for (const auto& msg : historyCopy) {
            sendToClient(clientId, msg);
        }
        sendToClient(clientId, "[SERVER] --- End History ---");
    }
}

bool Server::sendPrivateMessage(int senderId, std::string_view targetNickOrId, std::string_view message) {
    std::shared_ptr<ClientConnection> sender;
    std::shared_ptr<ClientConnection> target;

    std::string targetStr(targetNickOrId);
    if (!targetStr.empty() && targetStr[0] == '#') {
        targetStr = targetStr.substr(1);
    }

    {
        std::shared_lock<std::shared_mutex> lock(m_clientsMutex);
        auto sIt = m_clientsById.find(senderId);
        if (sIt != m_clientsById.end()) {
            sender = sIt->second;
        }

        // Try lookup by ID first if numeric
        try {
            int targetId = std::stoi(targetStr);
            auto tIt = m_clientsById.find(targetId);
            if (tIt != m_clientsById.end()) {
                target = tIt->second;
            }
        } catch (...) {}

        // Otherwise lookup by nickname
        if (!target) {
            for (const auto& pair : m_clientsById) {
                if (pair.second->getNickname() == targetStr) {
                    target = pair.second;
                    break;
                }
            }
        }
    }

    if (!sender || !target) return false;

    std::string pmForTarget = "[PM from " + sender->getNickname() + "]: " + std::string(message);
    std::string pmForSender = "[PM to " + target->getNickname() + "]: " + std::string(message);

    target->sendFrame(pmForTarget);
    sender->sendFrame(pmForSender);
    return true;
}

void Server::processClientMessage(int clientId, const std::string& message) {
    std::shared_ptr<ClientConnection> client;
    {
        std::shared_lock<std::shared_mutex> lock(m_clientsMutex);
        auto it = m_clientsById.find(clientId);
        if (it == m_clientsById.end()) return;
        client = it->second;
    }

    // Rate limiting check
    if (!client->checkRateLimit()) {
        client->sendFrame("[SERVER] Rate limit exceeded (Max 10 burst / 5 refill per sec). Please slow down.");
        Logger::getInstance().warn("Client rate limit exceeded", clientId);
        return;
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
                broadcastToChannel(client->getChannel(), sysMsg);
            } else {
                client->sendFrame("[SERVER] Usage: /nick <new_name>");
            }
        } else if (cmd == "/msg" || cmd == "/w" || cmd == "/dm") {
            std::string target;
            ss >> target;
            std::string pmBody;
            std::getline(ss, pmBody);
            // Trim leading space in pmBody
            size_t firstNonSpace = pmBody.find_first_not_of(" \t");
            if (firstNonSpace != std::string::npos) {
                pmBody = pmBody.substr(firstNonSpace);
            }

            if (!target.empty() && !pmBody.empty()) {
                if (!sendPrivateMessage(clientId, target, pmBody)) {
                    client->sendFrame("[SERVER] User '" + target + "' not found.");
                }
            } else {
                client->sendFrame("[SERVER] Usage: /msg <nickname|id> <message>");
            }
        } else if (cmd == "/join") {
            std::string channel;
            ss >> channel;
            if (!channel.empty()) {
                if (channel[0] != '#') {
                    channel = "#" + channel;
                }
                std::string oldChannel = client->getChannel();
                if (oldChannel != channel) {
                    broadcastToChannel(oldChannel, "[SERVER] " + client->getNickname() + " left channel " + oldChannel);
                    client->setChannel(channel);
                    broadcastToChannel(channel, "[SERVER] " + client->getNickname() + " joined channel " + channel);
                    sendChannelHistory(clientId, channel);
                    Logger::getInstance().info(client->getNickname() + " switched to channel " + channel, clientId);
                } else {
                    client->sendFrame("[SERVER] You are already in channel " + channel);
                }
            } else {
                client->sendFrame("[SERVER] Usage: /join <#channel>");
            }
        } else if (cmd == "/leave") {
            std::string oldChannel = client->getChannel();
            if (oldChannel != "#general") {
                broadcastToChannel(oldChannel, "[SERVER] " + client->getNickname() + " left channel " + oldChannel);
                client->setChannel("#general");
                broadcastToChannel("#general", "[SERVER] " + client->getNickname() + " joined channel #general");
                sendChannelHistory(clientId, "#general");
            } else {
                client->sendFrame("[SERVER] You are already in default channel #general");
            }
        } else if (cmd == "/rooms") {
            std::unordered_map<std::string, size_t> roomCounts;
            {
                std::shared_lock<std::shared_mutex> lock(m_clientsMutex);
                for (const auto& pair : m_clientsById) {
                    roomCounts[pair.second->getChannel()]++;
                }
            }
            std::stringstream roomsSs;
            roomsSs << "[SERVER] Active Channels (" << roomCounts.size() << "): ";
            bool first = true;
            for (const auto& [room, count] : roomCounts) {
                if (!first) roomsSs << ", ";
                roomsSs << room << " (" << count << " users)";
                first = false;
            }
            client->sendFrame(roomsSs.str());
        } else if (cmd == "/list") {
            std::stringstream listSs;
            std::string currentChan = client->getChannel();
            {
                std::shared_lock<std::shared_mutex> lock(m_clientsMutex);
                listSs << "[SERVER] Users in " << currentChan << ": ";
                bool first = true;
                for (const auto& pair : m_clientsById) {
                    if (pair.second->getChannel() == currentChan) {
                        if (!first) listSs << ", ";
                        listSs << pair.second->getNickname() << " (#" << pair.first << ")";
                        first = false;
                    }
                }
            }
            client->sendFrame(listSs.str());
        } else if (cmd == "/ping") {
            client->sendFrame("[SERVER] Pong! (Server active, port " + std::to_string(m_port) + ")");
        } else if (cmd == "/help") {
            std::string helpText =
                "[SERVER] Available Commands:\n"
                "  /nick <name>              - Change nickname\n"
                "  /msg <user|id> <message>  - Send direct private message\n"
                "  /join <#channel>          - Switch/Join room (e.g. /join #tech)\n"
                "  /leave                    - Return to #general channel\n"
                "  /rooms                    - List active channels and user counts\n"
                "  /list                     - List users in your current channel\n"
                "  /ping                     - Check server ping\n"
                "  /help                     - Display this command menu\n"
                "  /quit                     - Disconnect from server";
            client->sendFrame(helpText);
        } else if (cmd == "/quit") {
            client->sendFrame("[SERVER] Goodbye!");
            disconnectClient(client->getFd(), "User issued /quit");
        } else {
            client->sendFrame("[SERVER] Unknown command: " + cmd + ". Type /help for available commands.");
        }
    } else {
        // Normal chat broadcast scoped to client's channel
        std::string channel = client->getChannel();
        std::string formattedMsg = "[" + channel + "][" + client->getNickname() + "]: " + message;
        broadcastToChannel(channel, formattedMsg, clientId);
    }
}

void Server::disconnectClient(int fd, const std::string& reason) {
    std::shared_ptr<ClientConnection> client;
    {
        std::unique_lock<std::shared_mutex> lock(m_clientsMutex);
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
    broadcastToChannel(client->getChannel(), "[SERVER] " + client->getNickname() + " left the chat.");
}

void Server::broadcastToChannel(std::string_view channel, std::string_view message, int excludeClientId) {
    // 1. Store message in channel history ring buffer
    storeChannelHistory(std::string(channel), std::string(message));

    // 2. Pre-encode binary frame ONCE for all recipients
    std::vector<uint8_t> encodedFrame;
    Protocol::encodeToBuffer(message, encodedFrame);

    // 3. Acquire shared read lock to gather channel targets
    std::vector<std::shared_ptr<ClientConnection>> recipients;
    {
        std::shared_lock<std::shared_mutex> lock(m_clientsMutex);
        recipients.reserve(m_clientsById.size());
        for (const auto& [id, client] : m_clientsById) {
            if (id != excludeClientId && client->getChannel() == channel) {
                recipients.push_back(client);
            }
        }
    }

    // 4. Dispatch pre-encoded frame
    for (const auto& client : recipients) {
        client->sendPreencodedFrame(encodedFrame);
    }
}

void Server::broadcast(std::string_view message, int excludeClientId) {
    // 1. Pre-encode binary frame ONCE for all recipients
    std::vector<uint8_t> encodedFrame;
    Protocol::encodeToBuffer(message, encodedFrame);

    // 2. Acquire shared read lock to gather active client targets
    std::vector<std::shared_ptr<ClientConnection>> recipients;
    {
        std::shared_lock<std::shared_mutex> lock(m_clientsMutex);
        recipients.reserve(m_clientsById.size());
        for (const auto& [id, client] : m_clientsById) {
            if (id != excludeClientId) {
                recipients.push_back(client);
            }
        }
    }

    // 3. Dispatch pre-encoded frame to clients
    for (const auto& client : recipients) {
        client->sendPreencodedFrame(encodedFrame);
    }
}

void Server::sendToClient(int clientId, std::string_view message) {
    std::shared_ptr<ClientConnection> client;
    {
        std::shared_lock<std::shared_mutex> lock(m_clientsMutex);
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
            std::unique_lock<std::shared_mutex> lock(m_clientsMutex);
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
