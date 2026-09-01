#include "ClientConnection.hpp"
#include "Protocol.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#endif

#include <iostream>

#if !defined(_WIN32)
#include <netinet/tcp.h>
#endif

ClientConnection::ClientConnection(int id, int fd, const std::string& ipAddress, int port)
    : m_id(id), m_fd(fd), m_ipAddress(ipAddress), m_port(port) {
    m_nickname = "User_" + std::to_string(id);

    // Disable Nagle's algorithm for low-latency message delivery
    int flag = 1;
    setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
    setsockopt(m_fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&flag), sizeof(flag));
}

ClientConnection::~ClientConnection() {
    if (m_fd >= 0) {
#if defined(_WIN32)
        closesocket(m_fd);
#else
        close(m_fd);
#endif
        m_fd = -1;
    }
}

std::string ClientConnection::getNickname() const {
    std::lock_guard<std::mutex> lock(m_nickMutex);
    return m_nickname;
}

void ClientConnection::setNickname(const std::string& nick) {
    std::lock_guard<std::mutex> lock(m_nickMutex);
    m_nickname = nick;
}

bool ClientConnection::sendRawBytes(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (m_fd < 0 || size == 0) return false;

    size_t totalSent = 0;
    size_t bytesLeft = size;

    while (totalSent < size) {
        ssize_t sent = send(m_fd, reinterpret_cast<const char*>(data + totalSent), bytesLeft, MSG_NOSIGNAL);
        if (sent <= 0) {
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                usleep(500);
                continue;
            }
            return false;
        }
        totalSent += sent;
        bytesLeft -= sent;
    }
    return true;
}

bool ClientConnection::sendRawBytes(const std::vector<uint8_t>& data) {
    return sendRawBytes(data.data(), data.size());
}

bool ClientConnection::sendPreencodedFrame(const std::vector<uint8_t>& encodedFrame) {
    return sendRawBytes(encodedFrame.data(), encodedFrame.size());
}

bool ClientConnection::sendFrame(std::string_view textMessage) {
    std::vector<uint8_t> frame;
    Protocol::encodeToBuffer(textMessage, frame);
    return sendRawBytes(frame.data(), frame.size());
}
