#ifndef MIDDLEWARE_H
#define MIDDLEWARE_H

#include "http_request.h"
#include "http_response.h"
#include <functional>
#include <memory>

// 中间件基类
class Middleware {
public:
    virtual ~Middleware() = default;
    
    // 核心处理方法，返回true表示继续处理，false表示终止
    virtual bool process(const HttpRequest& request, HttpResponse& response) = 0;
    
    // 便捷创建方法
    static std::function<bool(const HttpRequest&, HttpResponse&)> 
    create(std::shared_ptr<Middleware> middleware);
};

// CORS中间件
class CorsMiddleware : public Middleware {
public:
    struct CorsConfig {
        std::vector<std::string> allowed_origins = {"*"};
        std::vector<std::string> allowed_methods = {"GET", "POST", "PUT", "DELETE", "OPTIONS"};
        std::vector<std::string> allowed_headers = {"Content-Type", "Authorization"};
        std::vector<std::string> exposed_headers;
        bool allow_credentials = false;
        int max_age = 3600;
    };
    
    explicit CorsMiddleware(const CorsConfig& config = CorsConfig{});
    bool process(const HttpRequest& request, HttpResponse& response) override;

private:
    CorsConfig config_;
    bool is_origin_allowed(const std::string& origin) const;
};

// 认证中间件
class AuthMiddleware : public Middleware {
public:
    using AuthValidator = std::function<bool(const std::string& token)>;
    
    explicit AuthMiddleware(AuthValidator validator);
    bool process(const HttpRequest& request, HttpResponse& response) override;

private:
    AuthValidator validator_;
    std::string extract_token(const HttpRequest& request) const;
};

// 日志中间件
class LoggingMiddleware : public Middleware {
public:
    LoggingMiddleware() = default;
    bool process(const HttpRequest& request, HttpResponse& response) override;

private:
    void log_request(const HttpRequest& request) const;
};

// 限流中间件
class RateLimitMiddleware : public Middleware {
public:
    struct RateLimitConfig {
        int max_requests = 100;      // 最大请求数
        int window_seconds = 3600;   // 时间窗口（秒）
        std::string key_generator = "ip";  // "ip" 或 "user"
    };
    
    explicit RateLimitMiddleware(const RateLimitConfig& config = RateLimitConfig{});
    bool process(const HttpRequest& request, HttpResponse& response) override;

private:
    RateLimitConfig config_;
    std::unordered_map<std::string, std::pair<int, time_t>> request_counts_;
    std::string generate_key(const HttpRequest& request) const;
    void cleanup_expired_entries();
};

// 静态文件中间件
class StaticFileMiddleware : public Middleware {
public:
    struct StaticConfig {
        std::string root_path;
        std::string index_file = "index.html";
        bool enable_directory_listing = false;
        std::vector<std::string> allowed_extensions;
    };
    
    explicit StaticFileMiddleware(const StaticConfig& config);
    bool process(const HttpRequest& request, HttpResponse& response) override;

private:
    StaticConfig config_;
    bool is_file_allowed(const std::string& file_path) const;
    void serve_file(const std::string& file_path, HttpResponse& response) const;
    void serve_directory(const std::string& dir_path, HttpResponse& response) const;
};

#endif // MIDDLEWARE_H