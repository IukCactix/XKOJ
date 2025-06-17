#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

class HttpRequest {
public:
    HttpRequest() = default;
    ~HttpRequest() = default;
    
    // 文件上传结构
    struct UploadedFile {
        std::string filename;
        std::string content_type;
        std::string content;
        size_t size;
        std::string field_name;  // 对应的表单字段名
        
        UploadedFile() : size(0) {}
    };
    
    // 基本信息访问
    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& query_string() const { return query_string_; }
    const std::string& version() const { return version_; }
    const std::string& body() const { return body_; }
    const std::string& client_ip() const { return client_ip_; }
    
    // 设置方法（主要用于解析过程）
    void set_method(const std::string& method) { method_ = method; }
    void set_path(const std::string& path) { path_ = path; }
    void set_query_string(const std::string& query) { query_string_ = query; }
    void set_version(const std::string& version) { version_ = version; }
    void set_body(const std::string& body) { body_ = body; }
    void set_client_ip(const std::string& ip) { client_ip_ = ip; }
    
    // 请求头操作
    void add_header(const std::string& key, const std::string& value);
    std::string get_header(const std::string& key) const;
    bool has_header(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }
    
    // 查询参数操作
    std::string get_param(const std::string& key) const;
    bool has_param(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& params() const { return params_; }
    
    // 路径参数操作（RESTful路由参数，如 /user/:id 中的 id）
    void set_path_param(const std::string& key, const std::string& value);
    std::string get_path_param(const std::string& key) const;
    bool has_path_param(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& path_params() const { return path_params_; }
    
    // 表单数据操作
    std::string get_form_data(const std::string& key) const;
    bool has_form_data(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& form_data() const { return form_data_; }
    
    // 内容类型检查
    bool is_json() const;
    bool is_form_data() const;
    bool is_multipart() const;
    bool is_xml() const;
    
    // JSON数据处理
    std::string get_json() const { return body_; }
    
    // 文件上传操作
    const std::vector<UploadedFile>& uploaded_files() const { return uploaded_files_; }
    const UploadedFile* get_uploaded_file(const std::string& field_name) const;
    std::vector<UploadedFile> get_uploaded_files(const std::string& field_name) const;
    bool has_uploaded_file(const std::string& field_name) const;
    size_t get_total_upload_size() const {
        size_t total = 0;
        for (const auto& file : uploaded_files_) {
            total += file.size;
        }
        return total;
    }
    
    // Cookie操作
    std::string get_cookie(const std::string& name) const;
    bool has_cookie(const std::string& name) const;
    const std::unordered_map<std::string, std::string>& cookies() const { return cookies_; }
    
    // 认证相关
    std::string get_auth_token() const;
    std::string get_basic_auth_username() const;
    std::string get_basic_auth_password() const;
    bool has_bearer_token() const;
    
    // 请求解析
    bool parse(const std::string& raw_request);
    
    // 实用方法
    bool is_ajax() const;
    bool is_secure() const;
    std::string get_user_agent() const;
    std::string get_referer() const;
    std::string get_content_type() const;
    size_t get_content_length() const;
    
    // 请求验证
    bool is_valid() const { return !method_.empty() && !path_.empty(); }
    
    // 调试信息
    std::string to_string() const;

private:
    // 基本请求信息
    std::string method_;
    std::string path_;
    std::string query_string_;
    std::string version_;
    std::string body_;
    std::string client_ip_;
    
    // 各种参数映射
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> params_;          // URL查询参数
    std::unordered_map<std::string, std::string> path_params_;     // 路径参数
    std::unordered_map<std::string, std::string> form_data_;       // 表单数据
    std::unordered_map<std::string, std::string> cookies_;         // Cookie数据
    
    // 文件上传
    std::vector<UploadedFile> uploaded_files_;
    std::unordered_map<std::string, std::vector<size_t>> file_field_mapping_;  // 字段名到文件索引
    
    // 认证信息缓存
    mutable std::string cached_auth_token_;
    mutable std::string cached_basic_username_;
    mutable std::string cached_basic_password_;
    mutable bool auth_parsed_ = false;
    
    // 解析辅助方法
    void parse_query_string();
    void parse_form_data();
    void parse_multipart_data();
    void parse_multipart_part(const std::string& part_data);
    void parse_cookies();
    void parse_auth_header() const;
    
    // 工具方法
    std::unordered_map<std::string, std::string> parse_url_encoded(const std::string& data) const;
    std::unordered_map<std::string, std::string> parse_header_parameters(const std::string& header_value) const;
    std::string url_decode(const std::string& encoded) const;
    std::string trim(const std::string& str) const;
    std::string to_lower(const std::string& str) const;
    
    // 常量
    static const size_t MAX_UPLOAD_SIZE = 100 * 1024 * 1024; // 100MB
    static const size_t MAX_FILES_COUNT = 50; // 最大文件数量
};

#endif // HTTP_REQUEST_H