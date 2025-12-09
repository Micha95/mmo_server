#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>
#include <deque>
#include <mutex>
#include <algorithm>
#include <ctime>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>

// Log levels
enum class LogLevel {
    DEBUG_EXTENDED,
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    NONE
};

extern LogLevel GLOBAL_LOG_LEVEL;
extern bool LOG_TO_FILE;
extern std::string LOG_FILE_DIR;
extern LogLevel GLOBAL_FILE_LOG_LEVEL;
extern std::string LOG_FILE_NAME;

inline LogLevel LogLevelFromString(const std::string& s) {
    if (s == "DEBUG") return LogLevel::DEBUG;
    if(s== "DEBUG_EXTENDED") return LogLevel::DEBUG_EXTENDED;
    if (s == "INFO") return LogLevel::INFO;
    if (s == "WARNING") return LogLevel::WARNING;
    if (s == "ERROR") return LogLevel::ERROR;
    if (s == "NONE") return LogLevel::NONE;
    return LogLevel::DEBUG;
}

inline void SetLogFileName(const std::string& name) {
    LOG_FILE_NAME = name;
}
inline std::string GetLogFileName() {
    return LOG_FILE_NAME;
}
inline void SetLogLevel(LogLevel level) {
    GLOBAL_LOG_LEVEL = level;
}

inline void SetFileLogLevel(LogLevel level) {
    GLOBAL_FILE_LOG_LEVEL = level;
}
inline void SetLogToFile(bool enable) {
    LOG_TO_FILE = enable;
}
inline void SetLogFileDir(const std::string& dir) {
    LOG_FILE_DIR = dir;
}
inline const char* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::DEBUG_EXTENDED: return "DEBUG_EXTENDED";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        default: return "LOG";
    }
}

// ANSI color codes for log levels
inline const char* LogLevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "\033[36m";    // Cyan
        case LogLevel::INFO: return "\033[32m";     // Dark-Green
        case LogLevel::WARNING: return "\033[33m";  // Yellow
        case LogLevel::ERROR: return "\033[31m";    // Red
        case LogLevel::NONE: return "\033[92m";      // Light GREEN
        default: return "\033[0m";                  // Reset
    }
}

inline std::string FormatTimestamp(std::time_t t) {
    char timebuf[32];
    std::tm tm;
    localtime_r(&t, &tm);
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(timebuf);
}

struct LogEntry {
    std::time_t timestamp;
    LogLevel level;
    std::string message;
};

extern std::deque<LogEntry> log_buffer;
extern std::mutex log_mutex;
extern std::queue<LogEntry> log_queue;
extern std::mutex log_queue_mutex;
extern std::condition_variable log_cv;
extern std::thread log_thread;
extern std::atomic<bool> log_thread_running;
static constexpr size_t LOG_BUFFER_MAX = 500;

inline void LogWorker() {
    while (log_thread_running) {
        std::unique_lock<std::mutex> lock(log_queue_mutex);
        log_cv.wait(lock, [] { return !log_queue.empty() || !log_thread_running; });
        while (!log_queue.empty()) {
            LogEntry entry = log_queue.front();
            log_queue.pop();
            lock.unlock();
            // In-memory buffer
            {
                std::lock_guard<std::mutex> buf_lock(log_mutex);
                log_buffer.push_back(entry);
                if (log_buffer.size() > LOG_BUFFER_MAX) log_buffer.pop_front();
            }
            // File logging
            if (LOG_TO_FILE && entry.level >= GLOBAL_FILE_LOG_LEVEL) {
                try {
                    std::filesystem::create_directories(LOG_FILE_DIR);
                    std::ofstream ofs(LOG_FILE_DIR + "/" + LOG_FILE_NAME, std::ios::app);
                    if (ofs.is_open()) {
                        ofs << FormatTimestamp(entry.timestamp) << " [" << LogLevelToString(entry.level) << "] " << entry.message << std::endl;
                    }
                } catch (...) {}
            }
            // Console
            if (entry.level >= GLOBAL_LOG_LEVEL) {
                std::ostream& out = (entry.level >= LogLevel::WARNING) ? std::cerr : std::cout;
                out << LogLevelColor(entry.level) << "[" << LogLevelToString(entry.level) << "] " << entry.message << "\033[0m" << std::endl;
            }
            lock.lock();
        }
    }
}

inline void StartLogThread() {
    log_thread_running = true;
    log_thread = std::thread(LogWorker);
}

inline void StopLogThread() {
    log_thread_running = false;
    log_cv.notify_all();
    if (log_thread.joinable()) log_thread.join();
}

inline void Log(LogLevel level, const std::string& msg) {
    std::time_t t = std::time(nullptr);
    {
        std::lock_guard<std::mutex> lock(log_queue_mutex);
        log_queue.push(LogEntry{t, level, msg});
    }
    log_cv.notify_one();
}

inline std::vector<LogEntry> GetRecentLogs(size_t max_count = 100, LogLevel min_level = LogLevel::DEBUG, const std::string& text_filter = "") {
    std::vector<LogEntry> result;
    std::lock_guard<std::mutex> lock(log_mutex);
    for (auto it = log_buffer.rbegin(); it != log_buffer.rend() && result.size() < max_count; ++it) {
        if (it->level < min_level) continue;
        if (!text_filter.empty() && it->message.find(text_filter) == std::string::npos) continue;
        result.push_back(*it);
    }
    std::reverse(result.begin(), result.end());
    return result;
}

// Set log level from string (e.g. from config)
LogLevel LogLevelFromString(const std::string& s);

#define LOG_DEBUG(msg)   Log(LogLevel::DEBUG, msg)
#define LOG_DEBUG_EXT(msg)   Log(LogLevel::DEBUG_EXTENDED, msg)
#define LOG_INFO(msg)    Log(LogLevel::INFO, msg)
#define LOG_WARNING(msg) Log(LogLevel::WARNING, msg)
#define LOG_ERROR(msg)   Log(LogLevel::ERROR, msg)
#define LOG_CONFIG(msg)   Log(LogLevel::INFO, msg)
#define LOG(msg)  Log(LogLevel::NONE, msg)
