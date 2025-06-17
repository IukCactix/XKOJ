#include "core/middleware.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>

// Middleware基类实现
std::function<bool(const HttpRequest&, HttpResponse&)> 
Middleware::create(std::shared_ptr<Middleware> middleware) {
    return [middleware](const HttpRequest& req, HttpResponse& res) -> bool {
        return middleware->process(req, res);
    };
}

// CORS中间件实现
CorsMiddleware::CorsMiddleware(const CorsConfig& config) : config_(config) {}

bool CorsMiddleware::process(const HttpRequest& request, HttpResponse& response) {
    std::string origin = request.get_header("Origin");
    
    // 检查来源是否被允许
    if (!origin.empty() && is_origin_allowed(origin)) {
        response.set_header("Access-Control-Allow-Origin", origin);
    } else if (config_.allowed_origins.size() == 1 && config_.allowed_origins[0] == "*") {
        response.set_header("Access-Control-Allow-Origin", "*");
    }
    
    // 设置允许的方法
    if (!config_.allowed_methods.empty()) {
        std::ostringstream methods;
        for (size_t i = 0; i < config_.allowed_methods.size(); ++i) {
            if (i > 0) methods << ", ";
            methods << config_.allowed_methods[i];
        }
        response.set_header("Access-Control-Allow-Methods", methods.str());
    }
    
    // 设置允许的头部
    if (!config_.allowed_headers.empty()) {
        std::ostringstream headers;
        for (size_t i = 0; i < config_.allowed_headers.size(); ++i) {
            if (i > 0) headers << ", ";
            headers << config_.allowed_headers[i];
        }
        response.set_header("Access-Control-Allow-Headers", headers.str());
    }
    
    // 设置暴露的头部
    if (!config_.exposed_headers.empty()) {
        std::ostringstream headers;
        for (size_t i = 0; i < config_.exposed_headers.size(); ++i) {
            if (i > 0) headers << ", ";
            headers << config_.exposed_headers[i];
        }
        response.set_header("Access-Control-Expose-Headers", headers.str());
    }
    
    // 设置凭据
    if (config_.allow_credentials) {
        response.set_header("Access-Control-Allow-Credentials", "true");
    }
    
    // 设置预检缓存时间
    if (request.method() == "OPTIONS") {
        response.set_header("Access-Control-Max-Age", std::to_string(config_.max_age));
        response.set_status(HttpStatus::NO_CONTENT);
        return false;  // OPTIONS请求到此为止
    }
    
    return true;  // 继续处理
}

bool CorsMiddleware::is_origin_allowed(const std::string& origin) const {
    for (const auto& allowed : config_.allowed_origins) {
        if (allowed == "*" || allowed == origin) {
            return true;
        }
        // 这里可以实现更复杂的匹配逻辑，如通配符匹配
    }
    return false;
}

// 认证中间件实现
AuthMiddleware::AuthMiddleware(AuthValidator validator) : validator_(std::move(validator)) {}

bool AuthMiddleware::process(const HttpRequest& request, HttpResponse& response) {
    std::string token = extract_token(request);
    
    if (token.empty()) {
        response.set_status(HttpStatus::UNAUTHORIZED);
        response.json("{\"error\": \"Missing authentication token\"}");
        return false;
    }
    
    if (!validator_(token)) {
        response.set_status(HttpStatus::UNAUTHORIZED);
        response.json("{\"error\": \"Invalid authentication token\"}");
        return false;
    }
    
    return true;  // 认证通过，继续处理
}

std::string AuthMiddleware::extract_token(const HttpRequest& request) const {
    std::string auth_header = request.get_header("Authorization");
    
    if (auth_header.empty()) {
        return "";
    }
    
    // 支持 "Bearer <token>" 格式
    const std::string bearer_prefix = "Bearer ";
    if (auth_header.substr(0, bearer_prefix.length()) == bearer_prefix) {
        return auth_header.substr(bearer_prefix.length());
    }
    
    // 支持直接的token
    return auth_header;
}

// 日志中间件实现
bool LoggingMiddleware::process(const HttpRequest& request, HttpResponse& response) {
    log_request(request);
    return true;  // 总是继续处理
}

void LoggingMiddleware::log_request(const HttpRequest& request) const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream log_entry;
    log_entry << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] ";
    log_entry << request.client_ip() << " ";
    log_entry << request.method() << " ";
    log_entry << request.path();
    
    if (!request.query_string().empty()) {
        log_entry << "?" << request.query_string();
    }
    
    log_entry << " " << request.version();
    
    std::string user_agent = request.get_header("User-Agent");
    if (!user_agent.empty()) {
        log_entry << " \"" << user_agent << "\"";
    }
    
    std::cout << log_entry.str() << std::endl;
}

// 限流中间件实现
RateLimitMiddleware::RateLimitMiddleware(const RateLimitConfig& config) : config_(config) {}

bool RateLimitMiddleware::process(const HttpRequest& request, HttpResponse& response) {
    std::string key = generate_key(request);
    auto now = std::time(nullptr);
    
    // 清理过期条目
    cleanup_expired_entries();
    
    auto& [count, window_start] = request_counts_[key];
    
    // 检查是否在当前时间窗口内
    if (now - window_start >= config_.window_seconds) {
        // 新的时间窗口
        count = 1;
        window_start = now;
    } else {
        // 在当前时间窗口内
        count++;
        
        if (count > config_.max_requests) {
            // 超过限制
            response.set_status(HttpStatus::TOO_MANY_REQUESTS);
            response.set_header("X-RateLimit-Limit", std::to_string(config_.max_requests));
            response.set_header("X-RateLimit-Remaining", "0");
            response.set_header("X-RateLimit-Reset", std::to_string(window_start + config_.window_seconds));
            response.json("{\"error\": \"Rate limit exceeded\"}");
            return false;
        }
    }
    
    // 设置速率限制头部
    response.set_header("X-RateLimit-Limit", std::to_string(config_.max_requests));
    response.set_header("X-RateLimit-Remaining", std::to_string(config_.max_requests - count));
    response.set_header("X-RateLimit-Reset", std::to_string(window_start + config_.window_seconds));
    
    return true;
}

std::string RateLimitMiddleware::generate_key(const HttpRequest& request) const {
    if (config_.key_generator == "ip") {
        return request.client_ip();
    } else if (config_.key_generator == "user") {
        // 这里需要从请求中提取用户信息
        // 可以从JWT token或session中获取
        std::string auth_header = request.get_header("Authorization");
        if (!auth_header.empty()) {
            return auth_header;  // 简化实现
        }
        return request.client_ip();  // fallback到IP
    }
    
    return request.client_ip();  // 默认使用IP
}

void RateLimitMiddleware::cleanup_expired_entries() {
    auto now = std::time(nullptr);
    
    for (auto it = request_counts_.begin(); it != request_counts_.end();) {
        if (now - it->second.second >= config_.window_seconds * 2) {
            it = request_counts_.erase(it);
        } else {
            ++it;
        }
    }
}

// 静态文件中间件实现
StaticFileMiddleware::StaticFileMiddleware(const StaticConfig& config) : config_(config) {}

bool StaticFileMiddleware::process(const HttpRequest& request, HttpResponse& response) {
    // 只处理GET请求
    if (request.method() != "GET" && request.method() != "HEAD") {
        return true;  // 继续处理其他路由
    }
    
    std::string file_path = config_.root_path + request.path();
    
    // 安全检查：防止目录遍历
    if (!is_file_allowed(file_path)) {
        response.set_status(HttpStatus::FORBIDDEN);
        return false;
    }
    
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0) {
        return true;  // 文件不存在，继续处理其他路由
    }
    
    if (S_ISDIR(file_stat.st_mode)) {
        // 是目录
        std::string index_path = file_path + "/" + config_.index_file;
        if (stat(index_path.c_str(), &file_stat) == 0) {
            serve_file(index_path, response);
        } else if (config_.enable_directory_listing) {
            serve_directory(file_path, response);
        } else {
            response.set_status(HttpStatus::FORBIDDEN);
        }
    } else {
        // 是文件
        serve_file(file_path, response);
    }
    
    return false;  // 静态文件处理完成，不继续处理其他路由
}

bool StaticFileMiddleware::is_file_allowed(const std::string& file_path) const {
    // 防止目录遍历攻击
    if (file_path.find("..") != std::string::npos) {
        return false;
    }
    
    // 检查扩展名限制
    if (!config_.allowed_extensions.empty()) {
        size_t dot_pos = file_path.find_last_of('.');
        if (dot_pos != std::string::npos) {
            std::string ext = file_path.substr(dot_pos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            auto it = std::find(config_.allowed_extensions.begin(), 
                               config_.allowed_extensions.end(), ext);
            return it != config_.allowed_extensions.end();
        }
        return false;  // 没有扩展名且有限制
    }
    
    return true;
}

void StaticFileMiddleware::serve_file(const std::string& file_path, HttpResponse& response) const {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        response.set_status(HttpStatus::INTERNAL_SERVER_ERROR);
        return;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    response.set_status(HttpStatus::OK);
    response.set_header("Content-Type", get_mime_type(file_path));
    response.set_body(content);
    
    // 设置缓存头
    response.set_header("Cache-Control", "public, max-age=3600");
    
    // 设置ETag (简化实现)
    std::hash<std::string> hasher;
    std::string etag = "\"" + std::to_string(hasher(content)) + "\"";
    response.set_header("ETag", etag);
}

void StaticFileMiddleware::serve_directory(const std::string& dir_path, HttpResponse& response) const {
    DIR* dir = opendir(dir_path.c_str());
    if (!dir) {
        response.set_status(HttpStatus::INTERNAL_SERVER_ERROR);
        return;
    }
    
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><title>Directory Listing</title>";
    html << "<style>body{font-family:monospace;margin:2em;}";
    html << "a{text-decoration:none;color:#0066cc;}";
    html << "a:hover{text-decoration:underline;}</style></head><body>";
    html << "<h1>Directory Listing</h1><hr><pre>";
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.' && strcmp(entry->d_name, "..") != 0) {
            continue;  // 跳过隐藏文件（除了..）
        }
        
        std::string name = entry->d_name;
        html << "<a href=\"" << name;
        if (entry->d_type == DT_DIR) {
            html << "/";
        }
        html << "\">" << name;
        if (entry->d_type == DT_DIR) {
            html << "/";
        }
        html << "</a>\n";
    }
    
    html << "</pre><hr></body></html>";
    closedir(dir);
    
    response.set_status(HttpStatus::OK);
    response.html(html.str());
}

std::string StaticFileMiddleware::get_mime_type(const std::string& file_path) const {
    // 重用HttpResponse中的实现
    HttpResponse temp_response;
    return temp_response.get_mime_type(file_path);
}