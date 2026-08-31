#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <fstream>
#include <mutex>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

enum class LogLevel {
    INFO,
    WARNING,
    ERROR,
    DEBUG
};

class Logger {
public:
    static Logger& getInstance();

    void init(const std::string& logFilePath = "");
    void log(LogLevel level, const std::string& message, int clientId = -1);

    void info(const std::string& message, int clientId = -1);
    void warn(const std::string& message, int clientId = -1);
    void error(const std::string& message, int clientId = -1);
    void debug(const std::string& message, int clientId = -1);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string getCurrentTimestamp();
    std::string logLevelToString(LogLevel level);

    std::mutex m_mutex;
    std::ofstream m_fileStream;
    bool m_isFileOpen{false};
};

#endif // LOGGER_HPP
