#ifndef CLIENTCONNECTION_HPP
#define CLIENTCONNECTION_HPP

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <chrono>
#include <string_view>

class ClientConnection {
public:
    ClientConnection(int id, int fd, const std::string& ipAddress, int port);
    ~ClientConnection();

    int getId() const { return m_id; }
    int getFd() const { return m_fd; }
    std::string getIpAddress() const { return m_ipAddress; }
    int getPort() const { return m_port; }

    std::string getNickname() const;
    void setNickname(const std::string& nick);

    std::string getChannel() const;
    void setChannel(const std::string& channel);

    // Token-bucket rate limiter (returns true if allowed, false if rate limited)
    bool checkRateLimit();

    void updateActivity();
    std::chrono::steady_clock::time_point getLastActivity() const;

    std::vector<uint8_t>& getReadBuffer() { return m_readBuffer; }

    // Thread-safe send of encoded binary frame directly to socket fd
    bool sendRawBytes(const uint8_t* data, size_t size);
    bool sendRawBytes(const std::vector<uint8_t>& data);
    bool sendFrame(std::string_view textMessage);
    bool sendPreencodedFrame(const std::vector<uint8_t>& encodedFrame);

private:
    int m_id;
    int m_fd;
    std::string m_ipAddress;
    int m_port;
    std::string m_nickname;
    std::string m_channel{"#general"};

    mutable std::mutex m_nickMutex;
    mutable std::mutex m_channelMutex;
    mutable std::mutex m_sendMutex;
    mutable std::mutex m_rateMutex;

    // Rate Limiter
    double m_tokens{10.0};
    static constexpr double MAX_TOKENS = 10.0;
    static constexpr double REFILL_RATE_PER_SEC = 5.0; // 5 tokens per second
    std::chrono::steady_clock::time_point m_lastRefill;
    std::chrono::steady_clock::time_point m_lastActivity;

    std::vector<uint8_t> m_readBuffer;
};

#endif // CLIENTCONNECTION_HPP

