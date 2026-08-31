#include "Server.hpp"
#include "Logger.hpp"

#include <iostream>
#include <string>
#include <csignal>
#include <thread>

static Server* g_serverPtr = nullptr;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[SIGNAL] Signal " << signal << " received. Initiating graceful shutdown..." << std::endl;
        if (g_serverPtr) {
            g_serverPtr->stop();
        }
    }
}

void printHelp(const char* progName) {
    std::cout << "Usage: " << progName << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  --port <port>         Port to listen on (default: 8080)\n"
              << "  --max-conn <number>   Max concurrent connections (default: 1000)\n"
              << "  --threads <number>    Thread pool size (default: hardware concurrency)\n"
              << "  --log-file <path>     Log file path (default: server.log)\n"
              << "  --help                Show this help message\n";
}

int main(int argc, char* argv[]) {
    int port = 8080;
    int maxConn = 1000;
    size_t threadPoolSize = std::thread::hardware_concurrency();
    if (threadPoolSize == 0) threadPoolSize = 4;
    std::string logFile = "server.log";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--max-conn" && i + 1 < argc) {
            maxConn = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            threadPoolSize = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--log-file" && i + 1 < argc) {
            logFile = argv[++i];
        } else if (arg == "--help") {
            printHelp(argv[0]);
            return 0;
        }
    }

    Server server(port, maxConn, threadPoolSize, logFile);
    g_serverPtr = &server;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    if (!server.init()) {
        std::cerr << "Failed to initialize server on port " << port << std::endl;
        return 1;
    }

    server.run();

    return 0;
}
