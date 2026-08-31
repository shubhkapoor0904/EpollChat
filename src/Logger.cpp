#include "Logger.hpp"

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (m_isFileOpen && m_fileStream.is_open()) {
        m_fileStream.close();
    }
}

void Logger::init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!logFilePath.empty()) {
        m_fileStream.open(logFilePath, std::ios::out | std::ios::app);
        if (m_fileStream.is_open()) {
            m_isFileOpen = true;
        } else {
            std::cerr << "[ERROR] Failed to open log file: " << logFilePath << std::endl;
        }
    }
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::DEBUG:   return "DEBUG";
        default:                return "UNKNOWN";
    }
}

void Logger::log(LogLevel level, const std::string& message, int clientId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string timestamp = getCurrentTimestamp();
    std::string lvlStr = logLevelToString(level);

    std::stringstream ss;
    ss << "[" << timestamp << "] [" << lvlStr << "]";
    if (clientId >= 0) {
        ss << " [Client #" << clientId << "]";
    }
    ss << " " << message;

    std::string formattedMsg = ss.str();

    std::cout << formattedMsg << std::endl;
    if (m_isFileOpen && m_fileStream.is_open()) {
        m_fileStream << formattedMsg << std::endl;
    }
}

void Logger::info(const std::string& message, int clientId) {
    log(LogLevel::INFO, message, clientId);
}

void Logger::warn(const std::string& message, int clientId) {
    log(LogLevel::WARNING, message, clientId);
}

void Logger::error(const std::string& message, int clientId) {
    log(LogLevel::ERROR, message, clientId);
}

void Logger::debug(const std::string& message, int clientId) {
    log(LogLevel::DEBUG, message, clientId);
}
