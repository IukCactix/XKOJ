#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <memory>
#include <chrono>
#include "http_server.h"

class HttpResponse {
public:
    // Cookie结构
    struct Cookie {
        std::string name;
        std::string value;
        std::string domain;
        std::string path = "/";
        int max_age = -1;  // -1表示会话cookie
        std::chrono::system_clock::time_point expires;
        bool secure = false;
        bool http_only = true;
        std::string same_site = "Lax";  // Strict, Lax, None
        
        Cookie() = default;
        Cookie(const std::string& n, const std::string& v) : name(n), value(v) {}
    };
    
    HttpResponse();
    ~HttpResponse() = default;
    
    // 状态码操作
    void set_status(HttpStatus status) { status_ = status; }
    HttpStatus status() const { return status_; }
    int status_code() const { return static_cast<int>(status_); }
    
    // 响应头操作
    void set_header(const std::string& key, const std::string& value);
    void add_header(const std::string& key, const std::string& value);
    std::string get_header(const std::string& key) const;
    bool has_header(const std::string& key) const;
    void remove_header(const std::string& key);
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }
    
    // 内容操作
    void set_body(const std::string& body);
    void set_body(const char* data, size_t length);
    void append_body(const std::string& content);
    void clear_body();
    const std::string& body() const { return body_; }
    size_t body_size() const { return body_.size(); }
    
    // 便捷响应方法
    void json(const std::string& json_str);
    void html(const std::string& html_str);
    void text(const std::string& text_str);
    void xml(const std::string& xml_str);
    void css(const std::string& css_str);
    void javascript(const std::string& js_str);
    
    // 文件响应
    void file(const std::string& file_path);
    void file(const std::string& file_path, const std::string& mime_type);
    void download(const std::string& file_path, const std::string& download_name = "");
    
    // 重定向
    void redirect(const std::string& url, HttpStatus status = HttpStatus::FOUND);
    void redirect_permanent(const std::string& url);
    void redirect_temporary(const std::string& url);
    
    // Cookie操作
    void set_cookie(const Cookie& cookie);
    void set_cookie(const std::string& name, const std::string& value);
    void set_cookie(const std::string& name, const std::string& value, int max_age);
    void clear_cookie(const std::string& name, const std::string& path = "/");
    const std::vector<Cookie>& cookies() const { return cookies_; }
    
    // 缓存控制
    void set_cache_control(const std::string& cache_control);
    void set_etag(const std::string& etag);
    void set_last_modified(const std::string& last_modified);
    void set_expires(const std::string& expires);
    void no_cache();
    void cache_forever();
    
    // CORS支持
    void enable_cors(const std::string& origin = "*");
    void set_cors_headers(const std::vector<std::string>& origins,
                         const std::vector<std::string>& methods,
                         const std::vector<std::string>& headers);
    
    // 内容编码
    void enable_gzip();
    void set_content_encoding(const std::string& encoding);
    
    // 响应构建
    std::string to_string() const;
    std::vector<char> to_bytes() const;
    
    // 便捷状态设置
    void ok() { set_status(HttpStatus::OK); }
    void created() { set_status(HttpStatus::CREATED); }
    void accepted() { set_status(HttpStatus::ACCEPTED); }
    void no_content() { set_status(HttpStatus::NO_CONTENT); }
    void bad_request() { set_status(HttpStatus::BAD_REQUEST); }
    void unauthorized() { set_status(HttpStatus::UNAUTHORIZED); }
    void forbidden() { set_status(HttpStatus::FORBIDDEN); }
    void not_found() { set_status(HttpStatus::NOT_FOUND); }
    void method_not_allowed() { set_status(HttpStatus::METHOD_NOT_ALLOWED); }
    void conflict() { set_status(HttpStatus::CONFLICT); }
    void unprocessable_entity() { set_status(HttpStatus::UNPROCESSABLE_ENTITY); }
    void too_many_requests() { set_status(HttpStatus::TOO_MANY_REQUESTS); }
    void internal_error() { set_status(HttpStatus::INTERNAL_SERVER_ERROR); }
    void not_implemented() { set_status(HttpStatus::NOT_IMPLEMENTED); }
    void service_unavailable() { set_status(HttpStatus::SERVICE_UNAVAILABLE); }
    
    // JSON响应快捷方法
    void json_success(const std::string& message = "Success");
    void json_error(const std::string& message, HttpStatus status = HttpStatus::BAD_REQUEST);
    void json_data(const std::string& data);
    
    // 模板响应
    void render_template(const std::string& template_path, 
                        const std::unordered_map<std::string, std::string>& variables = {});
    
    // 流式响应支持
    void start_streaming();
    void write_chunk(const std::string& chunk);
    void end_streaming();
    bool is_streaming() const { return streaming_; }
    
    // 响应验证
    bool is_valid() const;
    
    // 调试信息
    std::string debug_string() const;

private:
    HttpStatus status_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    std::vector<Cookie> cookies_;
    bool streaming_;
    bool headers_sent_;
    
    // 内部状态
    mutable bool content_length_set_;
    mutable std::string cached_response_;
    mutable bool cache_valid_;
    
    // 辅助方法
    std::string status_to_string(HttpStatus status) const;
    std::string cookie_to_string(const Cookie& cookie) const;
    std::string get_mime_type(const std::string& file_path) const;
    std::string get_current_time_string() const;
    std::string format_cookie_expires(const std::chrono::system_clock::time_point& expires) const;
    void update_content_length();
    void invalidate_cache();
    bool load_file_content(const std::string& file_path, std::string& content) const;
    
    // 模板引擎简单实现
    std::string process_template(const std::string& template_content,
                               const std::unordered_map<std::string, std::string>& variables) const;
    
    // 压缩支持
    std::string compress_gzip(const std::string& data) const;
    
    // MIME类型映射
    static const std::unordered_map<std::string, std::string> mime_types_;
    
    // 常用响应模板
    static const std::string ERROR_TEMPLATE;
    static const std::string SUCCESS_TEMPLATE;
};

#endif // HTTP_RESPONSE_H