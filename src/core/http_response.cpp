#include "core/http_response.h"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <iomanip>

HttpResponse::HttpResponse() : status_(HttpStatus::OK) {
    // 设置默认头部
    set_header("Content-Type", "text/html; charset=utf-8");
    set_header("Connection", "close");
}

void HttpResponse::set_header(const std::string& key, const std::string& value) {
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
    headers_[lower_key] = value;
}

void HttpResponse::add_header(const std::string& key, const std::string& value) {
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
    
    auto it = headers_.find(lower_key);
    if (it != headers_.end()) {
        it->second += ", " + value;
    } else {
        headers_[lower_key] = value;
    }
}

std::string HttpResponse::get_header(const std::string& key) const {
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
    auto it = headers_.find(lower_key);
    return it != headers_.end() ? it->second : "";
}

bool HttpResponse::has_header(const std::string& key) const {
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
    return headers_.find(lower_key) != headers_.end();
}

void HttpResponse::set_body(const std::string& body) {
    body_ = body;
    set_header("Content-Length", std::to_string(body_.size()));
}

void HttpResponse::append_body(const std::string& content) {
    body_ += content;
    set_header("Content-Length", std::to_string(body_.size()));
}

void HttpResponse::json(const std::string& json_str) {
    set_header("Content-Type", "application/json; charset=utf-8");
    set_body(json_str);
}

void HttpResponse::html(const std::string& html_str) {
    set_header("Content-Type", "text/html; charset=utf-8");
    set_body(html_str);
}

void HttpResponse::text(const std::string& text_str) {
    set_header("Content-Type", "text/plain; charset=utf-8");
    set_body(text_str);
}

void HttpResponse::file(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        set_status(HttpStatus::NOT_FOUND);
        html("<h1>404 Not Found</h1><p>File not found: " + file_path + "</p>");
        return;
    }
    
    // 读取文件内容
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    set_header("Content-Type", get_mime_type(file_path));
    set_body(content);
}

void HttpResponse::redirect(const std::string& url, HttpStatus status) {
    set_status(status);
    set_header("Location", url);
    
    std::string html_content = 
        "<!DOCTYPE html><html><head><title>Redirecting</title></head><body>"
        "<h1>Redirecting</h1><p>If you are not redirected automatically, "
        "<a href=\"" + url + "\">click here</a>.</p></body></html>";
    
    html(html_content);
}

void HttpResponse::set_cookie(const Cookie& cookie) {
    cookies_.push_back(cookie);
}

void HttpResponse::clear_cookie(const std::string& name, const std::string& path) {
    Cookie clear_cookie;
    clear_cookie.name = name;
    clear_cookie.value = "";
    clear_cookie.path = path;
    clear_cookie.max_age = 0;  // 立即过期
    cookies_.push_back(clear_cookie);
}

std::string HttpResponse::to_string() const {
    std::ostringstream response;
    
    // 状态行
    response << "HTTP/1.1 " << static_cast<int>(status_) << " " 
             << status_to_string(status_) << "\r\n";
    
    // 响应头
    for (const auto& [key, value] : headers_) {
        // 首字母大写的头部名称
        std::string header_name = key;
        bool capitalize_next = true;
        for (char& c : header_name) {
            if (capitalize_next) {
                c = std::toupper(c);
                capitalize_next = false;
            } else if (c == '-') {
                capitalize_next = true;
            }
        }
        response << header_name << ": " << value << "\r\n";
    }
    
    // Cookie
    for (const auto& cookie : cookies_) {
        response << "Set-Cookie: " << cookie_to_string(cookie) << "\r\n";
    }
    
    // 空行分隔符
    response << "\r\n";
    
    // 响应体
    response << body_;
    
    return response.str();
}

std::string HttpResponse::status_to_string(HttpStatus status) const {
    switch (status) {
        case HttpStatus::CONTINUE: return "Continue";
        case HttpStatus::SWITCHING_PROTOCOLS: return "Switching Protocols";
        case HttpStatus::OK: return "OK";
        case HttpStatus::CREATED: return "Created";
        case HttpStatus::ACCEPTED: return "Accepted";
        case HttpStatus::NO_CONTENT: return "No Content";
        case HttpStatus::RESET_CONTENT: return "Reset Content";
        case HttpStatus::PARTIAL_CONTENT: return "Partial Content";
        case HttpStatus::MULTIPLE_CHOICES: return "Multiple Choices";
        case HttpStatus::MOVED_PERMANENTLY: return "Moved Permanently";
        case HttpStatus::FOUND: return "Found";
        case HttpStatus::SEE_OTHER: return "See Other";
        case HttpStatus::NOT_MODIFIED: return "Not Modified";
        case HttpStatus::TEMPORARY_REDIRECT: return "Temporary Redirect";
        case HttpStatus::PERMANENT_REDIRECT: return "Permanent Redirect";
        case HttpStatus::BAD_REQUEST: return "Bad Request";
        case HttpStatus::UNAUTHORIZED: return "Unauthorized";
        case HttpStatus::PAYMENT_REQUIRED: return "Payment Required";
        case HttpStatus::FORBIDDEN: return "Forbidden";
        case HttpStatus::NOT_FOUND: return "Not Found";
        case HttpStatus::METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HttpStatus::NOT_ACCEPTABLE: return "Not Acceptable";
        case HttpStatus::REQUEST_TIMEOUT: return "Request Timeout";
        case HttpStatus::CONFLICT: return "Conflict";
        case HttpStatus::GONE: return "Gone";
        case HttpStatus::LENGTH_REQUIRED: return "Length Required";
        case HttpStatus::PAYLOAD_TOO_LARGE: return "Payload Too Large";
        case HttpStatus::URI_TOO_LONG: return "URI Too Long";
        case HttpStatus::UNSUPPORTED_MEDIA_TYPE: return "Unsupported Media Type";
        case HttpStatus::RANGE_NOT_SATISFIABLE: return "Range Not Satisfiable";
        case HttpStatus::EXPECTATION_FAILED: return "Expectation Failed";
        case HttpStatus::UNPROCESSABLE_ENTITY: return "Unprocessable Entity";
        case HttpStatus::TOO_MANY_REQUESTS: return "Too Many Requests";
        case HttpStatus::INTERNAL_SERVER_ERROR: return "Internal Server Error";
        case HttpStatus::NOT_IMPLEMENTED: return "Not Implemented";
        case HttpStatus::BAD_GATEWAY: return "Bad Gateway";
        case HttpStatus::SERVICE_UNAVAILABLE: return "Service Unavailable";
        case HttpStatus::GATEWAY_TIMEOUT: return "Gateway Timeout";
        case HttpStatus::HTTP_VERSION_NOT_SUPPORTED: return "HTTP Version Not Supported";
        default: return "Unknown Status";
    }
}

std::string HttpResponse::cookie_to_string(const Cookie& cookie) const {
    std::ostringstream oss;
    oss << cookie.name << "=" << cookie.value;
    
    if (!cookie.domain.empty()) {
        oss << "; Domain=" << cookie.domain;
    }
    
    if (!cookie.path.empty()) {
        oss << "; Path=" << cookie.path;
    }
    
    if (cookie.max_age >= 0) {
        oss << "; Max-Age=" << cookie.max_age;
    }
    
    if (cookie.secure) {
        oss << "; Secure";
    }
    
    if (cookie.http_only) {
        oss << "; HttpOnly";
    }
    
    if (!cookie.same_site.empty()) {
        oss << "; SameSite=" << cookie.same_site;
    }
    
    return oss.str();
}

std::string HttpResponse::get_mime_type(const std::string& file_path) const {
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "application/octet-stream";
    }
    
    std::string ext = file_path.substr(dot_pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    static std::unordered_map<std::string, std::string> mime_types = {
        {"html", "text/html; charset=utf-8"},
        {"htm", "text/html; charset=utf-8"},
        {"css", "text/css; charset=utf-8"},
        {"js", "application/javascript; charset=utf-8"},
        {"json", "application/json; charset=utf-8"},
        {"xml", "application/xml; charset=utf-8"},
        {"txt", "text/plain; charset=utf-8"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"ico", "image/x-icon"},
        {"pdf", "application/pdf"},
        {"zip", "application/zip"},
        {"mp4", "video/mp4"},
        {"mp3", "audio/mpeg"},
        {"wav", "audio/wav"},
        {"woff", "font/woff"},
        {"woff2", "font/woff2"},
        {"ttf", "font/ttf"},
        {"otf", "font/otf"}
    };
    
    auto it = mime_types.find(ext);
    return it != mime_types.end() ? it->second : "application/octet-stream";
}