#include "../include/Protocol.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <cstdint>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

std::atomic<bool> g_running{true};

void receiveThreadFunc(int socketFd) {
    std::vector<uint8_t> readBuf;
    uint8_t buffer[2048];

    while (g_running) {
        ssize_t bytesRead = recv(socketFd, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
        if (bytesRead > 0) {
            readBuf.insert(readBuf.end(), buffer, buffer + bytesRead);
            std::vector<std::string> messages;
            if (Protocol::decode(readBuf, messages)) {
                for (const auto& msg : messages) {
                    std::cout << "\n" << msg << "\n> " << std::flush;
                }
            }
        } else if (bytesRead == 0) {
            std::cout << "\n[CLIENT] Server closed connection." << std::endl;
            g_running = false;
            break;
        } else {
            if (g_running) {
                std::cout << "\n[CLIENT] Connection error." << std::endl;
                g_running = false;
            }
            break;
        }
    }
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 8080;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);

#if defined(_WIN32)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd < 0) {
        std::cerr << "Failed to create socket." << std::endl;
        return 1;
    }

    struct sockaddr_in servAddr;
    std::memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &servAddr.sin_addr);

    std::cout << "Connecting to chat server at " << host << ":" << port << "..." << std::endl;
    if (connect(sockFd, reinterpret_cast<struct sockaddr*>(&servAddr), sizeof(servAddr)) < 0) {
        std::cerr << "Connection failed!" << std::endl;
        return 1;
    }

    std::cout << "Connected! Type your message or /nick, /list, /quit." << std::endl;

    std::thread rxThread(receiveThreadFunc, sockFd);

    std::string input;
    while (g_running && std::getline(std::cin, input)) {
        if (input.empty()) continue;

        std::vector<uint8_t> frame = Protocol::encode(input);
        send(sockFd, reinterpret_cast<const char*>(frame.data()), frame.size(), 0);

        if (input == "/quit") {
            g_running = false;
            break;
        }
        std::cout << "> " << std::flush;
    }

    g_running = false;
#if defined(_WIN32)
    closesocket(sockFd);
    WSACleanup();
#else
    close(sockFd);
#endif

    if (rxThread.joinable()) {
        rxThread.join();
    }

    std::cout << "Exited chat client." << std::endl;
    return 0;
}
