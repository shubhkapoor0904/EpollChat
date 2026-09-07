#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <shared_mutex>
#include <atomic>
#include <mutex>

#include "ClientConnection.hpp"
#include "ThreadPool.hpp"

#if !defined(_WIN32)
#include <sys/epoll.h>
#endif

class Server {
public:
    Server(int port, int maxConnections, size_t threadPoolSize, const std::string& logFile = "");
    ~Server();

    bool init();
    void run();
    void stop();

    // Broadcast a text frame to all clients (or excluding a specific sender client ID)
    void broadcast(std::string_view message, int excludeClientId = -1);

    // Broadcast a text frame to all clients in a specific channel
    void broadcastToChannel(std::string_view channel, std::string_view message, int excludeClientId = -1);

    // Send a private message frame to a specific client by target nickname or target ID
    bool sendPrivateMessage(int senderId, std::string_view targetNickOrId, std::string_view message);

    // Send a message frame to a single client ID
    void sendToClient(int clientId, std::string_view message);

private:
    void acceptNewConnection();
    void handleClientRead(int fd);
    void disconnectClient(int fd, const std::string& reason);
    void processClientMessage(int clientId, const std::string& message);

    void storeChannelHistory(const std::string& channel, const std::string& message);
    void sendChannelHistory(int clientId, const std::string& channel);

    int setNonBlocking(int fd);

    int m_port;
    int m_maxConnections;
    int m_serverFd{-1};
    int m_epollFd{-1};

    std::atomic<bool> m_running{false};
    std::atomic<int> m_nextClientId{1};

    ThreadPool m_threadPool;
    std::string m_logFile;

    std::unordered_map<int, std::shared_ptr<ClientConnection>> m_clientsByFd;
    std::unordered_map<int, std::shared_ptr<ClientConnection>> m_clientsById;
    mutable std::shared_mutex m_clientsMutex;

    // Channel History (Ring Buffer per channel)
    std::unordered_map<std::string, std::vector<std::string>> m_channelHistory;
    mutable std::mutex m_historyMutex;
    static constexpr size_t MAX_HISTORY_PER_CHANNEL = 15;

    static constexpr int MAX_EVENTS = 1024;
    static constexpr size_t READ_BUFFER_SIZE = 8192;
};

#endif // SERVER_HPP

