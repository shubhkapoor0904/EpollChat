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

namespace Color {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* BOLD    = "\033[1m";
    constexpr const char* CYAN    = "\033[36m";  // Server msgs & notices
    constexpr const char* GREEN   = "\033[32m";  // Regular chat
    constexpr const char* MAGENTA = "\033[35m";  // Private Messages (DMs)
    constexpr const char* YELLOW  = "\033[33m";  // Channel / Join / Leave alerts
    constexpr const char* RED     = "\033[31m";  // Errors & Warnings
}

std::atomic<bool> g_running{true};

void printColorizedMessage(const std::string& msg) {
    std::cout << "\r\033[K"; // Clear current prompt line
    if (msg.rfind("[SERVER]", 0) == 0) {
        if (msg.find("Rate limit") != std::string::npos || msg.find("not found") != std::string::npos || msg.find("Unknown command") != std::string::npos) {
            std::cout << Color::RED << msg << Color::RESET << "\n";
        } else if (msg.find("joined") != std::string::npos || msg.find("left") != std::string::npos) {
            std::cout << Color::YELLOW << msg << Color::RESET << "\n";
        } else {
            std::cout << Color::CYAN << msg << Color::RESET << "\n";
        }
    } else if (msg.rfind("[PM from", 0) == 0 || msg.rfind("[PM to", 0) == 0) {
        std::cout << Color::MAGENTA << Color::BOLD << msg << Color::RESET << "\n";
    } else {
        std::cout << Color::GREEN << msg << Color::RESET << "\n";
    }
    std::cout << Color::BOLD << "> " << Color::RESET << std::flush;
}

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
                    printColorizedMessage(msg);
                }
            }
        } else if (bytesRead == 0) {
            std::cout << "\n" << Color::RED << "[CLIENT] Server closed connection." << Color::RESET << std::endl;
            g_running = false;
            break;
        } else {
            if (g_running) {
                std::cout << "\n" << Color::RED << "[CLIENT] Connection error." << Color::RESET << std::endl;
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

    std::cout << Color::CYAN << "Connecting to Epoll TCP Chat Server at " << host << ":" << port << "..." << Color::RESET << std::endl;
    if (connect(sockFd, reinterpret_cast<struct sockaddr*>(&servAddr), sizeof(servAddr)) < 0) {
        std::cerr << Color::RED << "Connection failed!" << Color::RESET << std::endl;
        return 1;
    }

    std::cout << Color::CYAN << "Connected! Type /help for commands or type to chat." << Color::RESET << std::endl;
    std::cout << Color::BOLD << "> " << Color::RESET << std::flush;

    std::thread rxThread(receiveThreadFunc, sockFd);

    std::string input;
    while (g_running && std::getline(std::cin, input)) {
        if (input.empty()) {
            std::cout << Color::BOLD << "> " << Color::RESET << std::flush;
            continue;
        }

        std::vector<uint8_t> frame = Protocol::encode(input);
        send(sockFd, reinterpret_cast<const char*>(frame.data()), frame.size(), 0);

        if (input == "/quit") {
            g_running = false;
            break;
        }
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

    std::cout << Color::YELLOW << "Exited chat client." << Color::RESET << std::endl;
    return 0;
}

