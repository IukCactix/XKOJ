#include "core/http_request.h"
#include <sstream>
#include <algorithm>

void HttpRequest::add_header(const std::string& key, const std::string& value) {
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
    headers_[lower_key] = value;
}

std::string HttpRequest::get_header(const std::string& key) const {
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
    auto it = headers_.find(lower_key);
    return it != headers_.end() ? it->second : "";
}

bool HttpRequest::has_header(const std::string& key) const {
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
    return headers_.find(lower_key) != headers_.end();
}

std::string HttpRequest::get_param(const std::string& key) const {
    auto it = params_.find(key);
    return it != params_.end() ? it->second : "";
}

bool HttpRequest::has_param(const std::string& key) const {
    return params_.find(key) != params_.end();
}

void HttpRequest::set_path_param(const std::string& key, const std::string& value) {
    path_params_[key] = value;
}

std::string HttpRequest::get_path_param(const std::string& key) const {
    auto it = path_params_.find(key);
    return it != path_params_.end() ? it->second : "";
}

std::string HttpRequest::get_form_data(const std::string& key) const {
    auto it = form_data_.find(key);
    return it != form_data_.end() ? it->second : "";
}

bool HttpRequest::has_form_data(const std::string& key) const {
    return form_data_.find(key) != form_data_.end();
}

bool HttpRequest::is_json() const {
    std::string content_type = get_header("Content-Type");
    return content_type.find("application/json") != std::string::npos;
}

bool HttpRequest::parse(const std::string& raw_request) {
    std::istringstream stream(raw_request);
    std::string line;
    
    // 解析请求行
    if (!std::getline(stream, line)) {
        return false;
    }
    
    // 移除回车符
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    
    std::istringstream line_stream(line);
    std::string path_with_query, version;
    if (!(line_stream >> method_ >> path_with_query >> version_)) {
        return false;
    }
    
    // 分离路径和查询字符串
    size_t query_pos = path_with_query.find('?');
    if (query_pos != std::string::npos) {
        path_ = path_with_query.substr(0, query_pos);
        query_string_ = path_with_query.substr(query_pos + 1);
        parse_query_string();
    } else {
        path_ = path_with_query;
    }
    
    version_ = version;
    
    // 解析请求头
    while (std::getline(stream, line) && !line.empty()) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // 去除前后空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            add_header(key, value);
        }
    }
    
    // 读取请求体
    std::string remaining((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
    body_ = remaining;
    
    // 解析表单数据
    if (has_header("Content-Type")) {
        std::string content_type = get_header("Content-Type");
        if (content_type.find("application/x-www-form-urlencoded") != std::string::npos) {
            parse_form_data();
        } else if (content_type.find("multipart/form-data") != std::string::npos) {
            parse_multipart_data();
        }
    }
    
    return true;
}

void HttpRequest::parse_query_string() {
    params_ = parse_url_encoded(query_string_);
}

void HttpRequest::parse_form_data() {
    form_data_ = parse_url_encoded(body_);
}

void HttpRequest::parse_multipart_data() {
    std::string content_type = get_header("Content-Type");
    size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos) {
        return;
    }
    
    // 提取boundary，处理可能的引号
    std::string boundary_value = content_type.substr(boundary_pos + 9);
    if (boundary_value.front() == '"' && boundary_value.back() == '"') {
        boundary_value = boundary_value.substr(1, boundary_value.length() - 2);
    }
    
    std::string boundary = "--" + boundary_value;
    std::string end_boundary = boundary + "--";
    
    // 解析multipart数据
    std::string data = body_;
    size_t pos = 0;
    
    // 跳过第一个boundary前的数据
    size_t first_boundary = data.find(boundary);
    if (first_boundary == std::string::npos) {
        return;
    }
    pos = first_boundary + boundary.length();
    
    while (pos < data.length()) {
        // 跳过boundary后的CRLF
        if (pos + 1 < data.length() && data.substr(pos, 2) == "\r\n") {
            pos += 2;
        } else if (pos < data.length() && data[pos] == '\n') {
            pos += 1;
        } else {
            break; // 格式错误
        }
        
        // 查找下一个boundary
        size_t next_boundary = data.find(boundary, pos);
        if (next_boundary == std::string::npos) {
            break;
        }
        
        // 提取当前part的数据
        std::string part_data = data.substr(pos, next_boundary - pos);
        
        // 去除part末尾的CRLF
        if (part_data.length() >= 2 && part_data.substr(part_data.length() - 2) == "\r\n") {
            part_data = part_data.substr(0, part_data.length() - 2);
        }
        
        // 解析这个part
        parse_multipart_part(part_data);
        
        // 移动到下一个boundary
        pos = next_boundary + boundary.length();
        
        // 检查是否是结束boundary
        if (pos + 1 < data.length() && data.substr(pos, 2) == "--") {
            break; // 到达结束boundary
        }
    }
}

void HttpRequest::parse_multipart_part(const std::string& part_data) {
    // 分离headers和body
    size_t headers_end = part_data.find("\r\n\r\n");
    if (headers_end == std::string::npos) {
        headers_end = part_data.find("\n\n");
        if (headers_end == std::string::npos) {
            return; // 格式错误
        }
    }
    
    std::string headers_str = part_data.substr(0, headers_end);
    std::string body_str = part_data.substr(headers_end + (part_data[headers_end] == '\r' ? 4 : 2));
    
    // 解析headers
    std::unordered_map<std::string, std::string> part_headers;
    std::istringstream header_stream(headers_str);
    std::string header_line;
    
    while (std::getline(header_stream, header_line)) {
        // 移除回车符
        if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
        }
        
        if (header_line.empty()) {
            continue;
        }
        
        size_t colon_pos = header_line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = header_line.substr(0, colon_pos);
            std::string value = header_line.substr(colon_pos + 1);
            
            // 去除前后空格
            key = trim(key);
            value = trim(value);
            
            // 转换为小写
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            part_headers[key] = value;
        }
    }
    
    // 解析Content-Disposition header
    auto disposition_it = part_headers.find("content-disposition");
    if (disposition_it == part_headers.end()) {
        return; // 必须有Content-Disposition
    }
    
    std::string disposition = disposition_it->second;
    
    // 解析disposition参数
    std::unordered_map<std::string, std::string> disp_params = parse_header_parameters(disposition);
    
    // 检查是否是form-data
    if (disp_params.find("form-data") == disp_params.end()) {
        return; // 只处理form-data
    }
    
    std::string field_name = disp_params["name"];
    if (field_name.empty()) {
        return; // name参数是必需的
    }
    
    // 检查是否是文件上传
    if (disp_params.find("filename") != disp_params.end()) {
        // 文件上传
        UploadedFile file;
        file.filename = disp_params["filename"];
        file.content = body_str;
        file.size = body_str.length();
        
        // 获取Content-Type
        auto content_type_it = part_headers.find("content-type");
        if (content_type_it != part_headers.end()) {
            file.content_type = content_type_it->second;
        } else {
            file.content_type = "application/octet-stream";
        }
        
        uploaded_files_.push_back(file);
        
        // 同时也作为表单字段存储文件名
        form_data_[field_name] = file.filename;
    } else {
        // 普通表单字段
        form_data_[field_name] = body_str;
    }
}

std::unordered_map<std::string, std::string> HttpRequest::parse_header_parameters(const std::string& header_value) const{
    std::unordered_map<std::string, std::string> params;
    
    std::istringstream stream(header_value);
    std::string token;
    
    // 第一个token通常是主要的值（如form-data）
    if (std::getline(stream, token, ';')) {
        params[trim(token)] = "";
    }
    
    // 解析参数
    while (std::getline(stream, token, ';')) {
        token = trim(token);
        if (token.empty()) {
            continue;
        }
        
        size_t eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = trim(token.substr(0, eq_pos));
            std::string value = trim(token.substr(eq_pos + 1));
            
            // 移除引号
            if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }
            
            params[key] = value;
        }
    }
    
    return params;
}

std::string HttpRequest::trim(const std::string& str) const {
    const std::string whitespace = " \t\r\n";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

// 添加获取上传文件的便捷方法
const HttpRequest::UploadedFile* HttpRequest::get_uploaded_file(const std::string& field_name) const {
    // 通过字段名查找对应的上传文件
    // 这需要在parse_multipart_part中建立field_name到文件的映射
    for (const auto& file : uploaded_files_) {
        // 这里需要额外的映射机制，简化实现中直接遍历
        if (get_form_data(field_name) == file.filename) {
            return &file;
        }
    }
    return nullptr;
}

bool HttpRequest::has_uploaded_file(const std::string& field_name) const {
    return get_uploaded_file(field_name) != nullptr;
}

// 获取所有上传的文件
std::vector<HttpRequest::UploadedFile> HttpRequest::get_uploaded_files(const std::string& field_name) const {
    std::vector<UploadedFile> result;
    
    // 支持同名字段的多文件上传
    for (const auto& file : uploaded_files_) {
        // 这里需要更完善的映射机制
        // 简化实现中假设文件名匹配
        if (get_form_data(field_name) == file.filename) {
            result.push_back(file);
        }
    }
    
    return result;
}

std::unordered_map<std::string, std::string> HttpRequest::parse_url_encoded(const std::string& data) const {
    std::unordered_map<std::string, std::string> result;
    
    std::istringstream stream(data);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
            
            // URL解码
            key = url_decode(key);
            value = url_decode(value);
            
            result[key] = value;
        }
    }
    
    return result;
}

std::string HttpRequest::url_decode(const std::string& encoded) const {
    std::string decoded;
    decoded.reserve(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            std::string hex = encoded.substr(i + 1, 2);
            try {
                int value = std::stoi(hex, nullptr, 16);
                decoded.push_back(static_cast<char>(value));
                i += 2;
            } catch (...) {
                decoded.push_back(encoded[i]);
            }
        }
        else if (encoded[i] == '+') {
            decoded.push_back(' ');
        } 
        else {
            decoded.push_back(encoded[i]);
        }
    }
    return decoded;
}