#include "ClientConnection.hpp"
#include "Protocol.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#endif

#include <iostream>

ClientConnection::ClientConnection(int id, int fd, const std::string& ipAddress, int port)
    : m_id(id), m_fd(fd), m_ipAddress(ipAddress), m_port(port) {
    m_nickname = "User_" + std::to_string(id);
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

bool ClientConnection::sendRawBytes(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (m_fd < 0) return false;

    size_t totalSent = 0;
    size_t bytesLeft = data.size();

    while (totalSent < data.size()) {
        ssize_t sent = send(m_fd, reinterpret_cast<const char*>(data.data() + totalSent), bytesLeft, MSG_NOSIGNAL);
        if (sent <= 0) {
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Socket output buffer full; in production could buffer output for epoll EPOLLOUT.
                // For direct send: retry brief pause.
                usleep(1000);
                continue;
            }
            return false;
        }
        totalSent += sent;
        bytesLeft -= sent;
    }
    return true;
}

bool ClientConnection::sendFrame(const std::string& textMessage) {
    std::vector<uint8_t> frame = Protocol::encode(textMessage);
    return sendRawBytes(frame);
}
