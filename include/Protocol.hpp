#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <vector>
#include <string>
#include <string_view>
#include <cstdint>

class Protocol {
public:
    static constexpr size_t HEADER_SIZE = 4; // 4-byte big-endian length header
    static constexpr size_t MAX_PAYLOAD_SIZE = 64 * 1024; // 64 KB max payload safety limit

    // Encodes a text payload into binary frame [4-byte uint32 length][payload]
    static std::vector<uint8_t> encode(std::string_view payload);

    // Encodes a text payload directly into a target buffer (eliminates vector allocation)
    static void encodeToBuffer(std::string_view payload, std::vector<uint8_t>& outBuffer);

    // Decodes complete binary frames from buffer accumulator.
    // Extracts framed strings and erases processed bytes from readBuffer.
    static bool decode(std::vector<uint8_t>& buffer, std::vector<std::string>& outMessages);
};

#endif // PROTOCOL_HPP
