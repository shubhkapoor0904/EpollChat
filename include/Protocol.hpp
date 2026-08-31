#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <vector>
#include <string>
#include <cstdint>

class Protocol {
public:
    static const size_t HEADER_SIZE = 4; // 4-byte big-endian length header
    static const size_t MAX_PAYLOAD_SIZE = 64 * 1024; // 64 KB max payload safety limit

    // Encodes a text payload into binary frame [4-byte uint32 length][payload]
    static std::vector<uint8_t> encode(const std::string& payload);

    // Decodes complete binary frames from buffer accumulator.
    // Extracts framed strings and erases processed bytes from readBuffer.
    static bool decode(std::vector<uint8_t>& buffer, std::vector<std::string>& outMessages);
};

#endif // PROTOCOL_HPP
