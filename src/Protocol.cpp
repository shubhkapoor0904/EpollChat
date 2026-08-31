#include "Protocol.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <cstring>
#include <stdexcept>

std::vector<uint8_t> Protocol::encode(const std::string& payload) {
    uint32_t payloadLen = static_cast<uint32_t>(payload.size());
    uint32_t netLen = htonl(payloadLen);

    std::vector<uint8_t> frame(HEADER_SIZE + payloadLen);
    std::memcpy(frame.data(), &netLen, HEADER_SIZE);
    std::memcpy(frame.data() + HEADER_SIZE, payload.data(), payloadLen);

    return frame;
}

bool Protocol::decode(std::vector<uint8_t>& buffer, std::vector<std::string>& outMessages) {
    bool decodedAny = false;

    while (buffer.size() >= HEADER_SIZE) {
        uint32_t netLen = 0;
        std::memcpy(&netLen, buffer.data(), HEADER_SIZE);
        uint32_t payloadLen = ntohl(netLen);

        if (payloadLen > MAX_PAYLOAD_SIZE) {
            // Corrupt or invalid frame length safety guard
            throw std::runtime_error("Protocol violation: payload size exceeds maximum limit (" + std::to_string(payloadLen) + " bytes)");
        }

        size_t totalFrameSize = HEADER_SIZE + payloadLen;
        if (buffer.size() < totalFrameSize) {
            // Partial frame; wait for remaining bytes
            break;
        }

        // Extract payload
        std::string message(reinterpret_cast<const char*>(buffer.data() + HEADER_SIZE), payloadLen);
        outMessages.push_back(std::move(message));
        decodedAny = true;

        // Erase frame from buffer
        buffer.erase(buffer.begin(), buffer.begin() + totalFrameSize);
    }

    return decodedAny;
}
