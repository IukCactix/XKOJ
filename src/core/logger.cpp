#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <chrono>

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& log_file, LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    level_ = level;
    
    if (!log_file.empty()) {
        file_stream_ = std::make_unique<std::ofstream>(log_file, std::ios::app);
        if (!file_stream_->is_open()) {
            std::cerr << "Failed to open log file: " << log_file << std::endl;
            file_stream_.reset();
        }
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string timestamp = get_timestamp();
    std::string level_str = level_to_string(level);
    
    std::ostringstream oss;
    oss << "[" << timestamp << "] [" << level_str << "] " << message;
    
    std::string log_line = oss.str();
    
    // 输出到控制台
    std::cout << log_line << std::endl;
    
    // 输出到文件
    if (file_stream_ && file_stream_->is_open()) {
        *file_stream_ << log_line << std::endl;
        file_stream_->flush();
    }
}

void Logger::debug(const std::string& message) { log(LogLevel::DEBUG, message); }
void Logger::info(const std::string& message) { log(LogLevel::INFO, message); }
void Logger::warn(const std::string& message) { log(LogLevel::WARN, message); }
void Logger::error(const std::string& message) { log(LogLevel::ERROR, message); }
void Logger::fatal(const std::string& message) { log(LogLevel::FATAL, message); }

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}