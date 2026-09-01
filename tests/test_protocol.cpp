#include <catch2/catch_test_macros.hpp>
#include "Protocol.hpp"

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

TEST_CASE("Protocol framing roundtrip", "[Protocol]") {
    std::string originalMsg = "Hello, Epoll Chat Server!";
    std::vector<uint8_t> frame = Protocol::encode(originalMsg);

    REQUIRE(frame.size() == Protocol::HEADER_SIZE + originalMsg.size());

    // Verify 4-byte big endian header length
    uint32_t netLen = 0;
    std::memcpy(&netLen, frame.data(), Protocol::HEADER_SIZE);
    uint32_t hostLen = ntohl(netLen);
    REQUIRE(hostLen == originalMsg.size());

    // Decode frame
    std::vector<uint8_t> readBuf = frame;
    std::vector<std::string> decodedMsgs;
    bool success = Protocol::decode(readBuf, decodedMsgs);

    REQUIRE(success == true);
    REQUIRE(decodedMsgs.size() == 1);
    REQUIRE(decodedMsgs[0] == originalMsg);
    REQUIRE(readBuf.empty() == true);
}

TEST_CASE("Protocol encodeToBuffer test", "[Protocol]") {
    std::string msg = "Zero Allocation Buffer Encoding Test";
    std::vector<uint8_t> buffer;
    Protocol::encodeToBuffer(msg, buffer);

    std::vector<std::string> decodedMsgs;
    bool success = Protocol::decode(buffer, decodedMsgs);

    REQUIRE(success == true);
    REQUIRE(decodedMsgs.size() == 1);
    REQUIRE(decodedMsgs[0] == msg);
}

TEST_CASE("Multiple frames accumulated in buffer", "[Protocol]") {
    std::string msg1 = "First Message";
    std::string msg2 = "Second Message";
    std::string msg3 = "Third Message";

    std::vector<uint8_t> frame1 = Protocol::encode(msg1);
    std::vector<uint8_t> frame2 = Protocol::encode(msg2);
    std::vector<uint8_t> frame3 = Protocol::encode(msg3);

    std::vector<uint8_t> combinedBuffer;
    combinedBuffer.insert(combinedBuffer.end(), frame1.begin(), frame1.end());
    combinedBuffer.insert(combinedBuffer.end(), frame2.begin(), frame2.end());
    combinedBuffer.insert(combinedBuffer.end(), frame3.begin(), frame3.end());

    std::vector<std::string> decodedMsgs;
    bool success = Protocol::decode(combinedBuffer, decodedMsgs);

    REQUIRE(success == true);
    REQUIRE(decodedMsgs.size() == 3);
    REQUIRE(decodedMsgs[0] == msg1);
    REQUIRE(decodedMsgs[1] == msg2);
    REQUIRE(decodedMsgs[2] == msg3);
    REQUIRE(combinedBuffer.empty() == true);
}

TEST_CASE("Partial read and fragmented buffer decoding", "[Protocol]") {
    std::string msg = "Fragmented Payload Test";
    std::vector<uint8_t> frame = Protocol::encode(msg);

    std::vector<uint8_t> partialBuffer(frame.begin(), frame.begin() + 10);
    std::vector<std::string> decodedMsgs;

    // Decoding partial frame should return false and leave buffer untouched
    bool success = Protocol::decode(partialBuffer, decodedMsgs);
    REQUIRE(success == false);
    REQUIRE(decodedMsgs.empty() == true);
    REQUIRE(partialBuffer.size() == 10);

    // Append remainder of frame
    partialBuffer.insert(partialBuffer.end(), frame.begin() + 10, frame.end());
    success = Protocol::decode(partialBuffer, decodedMsgs);

    REQUIRE(success == true);
    REQUIRE(decodedMsgs.size() == 1);
    REQUIRE(decodedMsgs[0] == msg);
    REQUIRE(partialBuffer.empty() == true);
}

TEST_CASE("Oversized payload limit exception", "[Protocol]") {
    std::vector<uint8_t> maliciousBuffer(Protocol::HEADER_SIZE, 0);
    uint32_t invalidLen = htonl(Protocol::MAX_PAYLOAD_SIZE + 100);
    std::memcpy(maliciousBuffer.data(), &invalidLen, Protocol::HEADER_SIZE);

    std::vector<std::string> decodedMsgs;
    REQUIRE_THROWS_AS(Protocol::decode(maliciousBuffer, decodedMsgs), std::runtime_error);
}
