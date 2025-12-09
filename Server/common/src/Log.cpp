#include "Log.h"
#include <deque>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>
#include <atomic>

// Global log buffer and mutex definitions
std::deque<LogEntry> log_buffer;
std::mutex log_mutex;
std::queue<LogEntry> log_queue;
std::mutex log_queue_mutex;
std::condition_variable log_cv;
std::thread log_thread;
std::atomic<bool> log_thread_running = false;

LogLevel GLOBAL_LOG_LEVEL = LogLevel::DEBUG;
LogLevel GLOBAL_FILE_LOG_LEVEL = LogLevel::DEBUG;
bool LOG_TO_FILE = false;
std::string LOG_FILE_DIR = "./logs";
std::string LOG_FILE_NAME = "server.log";