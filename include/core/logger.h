#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4
};

class Logger {
public:
    static Logger& instance();
    
    void init(const std::string& log_file = "", LogLevel level = LogLevel::INFO);
    
    void log(LogLevel level, const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);
    
    void set_level(LogLevel level) { level_ = level; }
    
private:
    Logger() = default;
    LogLevel level_ = LogLevel::INFO;
    std::unique_ptr<std::ofstream> file_stream_;
    std::mutex mutex_;
    
    std::string level_to_string(LogLevel level);
    std::string get_timestamp();
};

// 便捷宏定义
#define LOG_DEBUG(msg) Logger::instance().debug(msg)
#define LOG_INFO(msg) Logger::instance().info(msg)
#define LOG_WARN(msg) Logger::instance().warn(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)
#define LOG_FATAL(msg) Logger::instance().fatal(msg)

#endif // LOGGER_H