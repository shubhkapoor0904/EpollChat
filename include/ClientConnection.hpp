#ifndef CLIENTCONNECTION_HPP
#define CLIENTCONNECTION_HPP

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

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

    mutable std::mutex m_nickMutex;
    mutable std::mutex m_sendMutex;

    std::vector<uint8_t> m_readBuffer;
};

#endif // CLIENTCONNECTION_HPP
