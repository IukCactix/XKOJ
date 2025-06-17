#include "core/http_server.h"
#include "core/http_request.h"
#include "core/http_response.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include <chrono>
#include <iomanip>

HttpServer* HttpServer::instance_ = nullptr;

Route::Route(HttpMethod m, const std::string& path, RouteHandler h)
    : method(m), original_path(path), handler(std::move(h)) {
    compile_path(path);
}

void Route::compile_path(const std::string& path) {
    std::string pattern = path;
    std::regex param_regex(R"(:([a-zA-Z_][a-zA-Z0-9_]*))");
    std::sregex_iterator iter(pattern.begin(), pattern.end(), param_regex);
    std::sregex_iterator end;

    for(; iter != end; ++iter) {
        param_names.push_back(iter->str(1));
    }

    // 将路径参数转换为正则表达式
    pattern = std::regex_replace(pattern, param_regex, R"(([^/]+))");
    
    // 转义特殊字符
    pattern = std::regex_replace(pattern, std::regex(R"(\.)"), R"(\.)");
    pattern = std::regex_replace(pattern, std::regex(R"(\+)"), R"(\+)");
    pattern = std::regex_replace(pattern, std::regex(R"(\*)"), R"(.*)");
    
    // 添加开始和结束锚点
    pattern = "^" + pattern + "$";

    this->pattern = std::regex(pattern);
}

ThreadPool::ThreadPool(size_t num_threads) : stop_(false) {
    for(size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_thread() {
    while(true) {
        Task task([]{});
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this]{return stop_ || !tasks_.empty();}); 
            if(stop_ && tasks_.empty()) {
                return ;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task.function();
    }
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();

    for(std::thread& worker : workers_) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

HttpServer::HttpServer(const ServerConfig& config)
    : config_(config)
    , running_(false)
    , shutting_down_(false)
    , server_fd_(-1)
    , epoll_fd_(-1)
    , thread_pool_(std::make_unique<ThreadPool>(config.thread_pool_size)) {
    
    instance_ = this;
    stats_.start_time = std::chrono::steady_clock::now();

    // 设置默认错误处理器
    default_error_handler_ = [this](const HttpRequest& req, HttpResponse& res, int error_code) {
        res.set_status(static_cast<HttpStatus>(error_code));
        res.set_header("Content-Type", "text/html; charset=utf-8");

        std::ostringstream html;
        html << "<!DOCTYPE html><html><head><title>Error " << error_code 
             << "</title></head><body>";
        html << "<h1>Error " << error_code << "</h1>";
        html << "<p>" << status_to_string(static_cast<HttpStatus>(error_code)) << "</p>";
        html << "<hr><p>" << config_.server_name << "</p>";
        html << "</body></html>";

        res.set_body(html.str());
    };
    
    setup_signal_handlers();
}

HttpServer::~HttpServer() {
    stop();
    instance_ = nullptr;
}

bool HttpServer::start() {
    if(running_.load()) {
        log("INFO", "Server is already running");
        return true;
    }
    log("INFO", "Starting HTTP server on" + config_.host + ":" + std::to_string(config_.port));
    if(!create_socket()) {
        log("ERROR", "Failed to create socket");
        return false;
    }
    if(!bind_socket()) {
        log("ERROR", "Failed to bind socket");
        return false;
    }
    if(!listen_socket()){
        log("ERROR", "Failed to listen on socket");
        return false;
    }
    if(!setup_epoll()){
        log("ERROR", "Failed to setup epoll");
        return false;
    }

    running_.store(true);

    main_thread_ = std::thread(&HttpServer::main_loop, this);
    cleanup_thread_ = std::thread(&HttpServer::cleanup_loop, this);

    log("INFO", "Server started successfully");
    return true;
}

void HttpServer::stop() {
    if(!running_.load()){
        return ;
    }
    log("INFO", "Stopping HTTP server");

    shutting_down_.store(true);
    running_.store(false);

    if(server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if(epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    if(main_thread_.joinable()) {
        main_thread_.join();
    }
    if(cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for(auto& [fd, conn] : connections_) {
            close(fd);
        }
        connections_.clear();
    }
    log("INFO", "Server stopped");
}

bool HttpServer::create_socket() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd_ < 0) {
        log("ERROR", "Socket creation failed: " + std::string(strerror(errno)));
        return false;
    }
    int opt = 1;
    if(setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log("ERROR", "Failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    //设置非阻塞模式
    int flags = fcntl(server_fd_, F_GETFL, 0);
    if(flags < 0 || fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        log("ERROR", "Failed to set non-blocking mode: " + std::string(strerror(errno)));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    return true;
}

bool HttpServer::bind_socket() {
    memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(config_.port);

    if(config_.host == "0.0.0.0" || config_.host == "*"){
        server_addr_.sin_addr.s_addr = INADDR_ANY;
    }
    else {
        if (inet_pton(AF_INET, config_.host.c_str(), &server_addr_.sin_addr) <= 0) {
            log("ERROR", "Invalid host address: " + config_.host);
            return false;
        }
    }

    if(bind(server_fd_, (struct sockaddr*)&server_addr_, sizeof(server_addr_)) < 0) {
        log("ERROR", "Bind failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

bool HttpServer::listen_socket() {
    if(listen(server_fd_, config_.max_connections) < 0) {
        log("ERROR", "Listen failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

bool HttpServer::setup_epoll() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if(epoll_fd_ < 0) {
        log("ERROR", "Epoll create failed: " + std::string(strerror(errno)));
        return false;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd_;
    if(epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev) < 0) {
        log("ERROR", "Epoll ctl failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

void HttpServer::main_loop() {
    const int MAX_EVENTS = 1000;
    struct epoll_event events[MAX_EVENTS];

    while(running_.load()) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1000);
        if(nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            log("ERROR", "Epoll wait failed: " + std::string(strerror(errno)));
            break;
        }
        for(int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            if(fd == server_fd_) {
                accept_connection();
            }
            else {
                if(events[i].events & (EPOLLERR | EPOLLHUP)) {
                    close_connection(fd);
                }
                else if(events[i].events & EPOLLIN) {
                    handle_client_data(fd);
                }
            }
        }
    }
}

void HttpServer::cleanup_loop() {
    while(running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        auto now = std::chrono::steady_clock::now();
        std::vector<int> to_close;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            for(auto& [fd, conn] : connections_) {
                auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(now - conn.last_activity).count();
                if(idle_time > config_.timeout_seconds) {
                    to_close.push_back(fd);
                }
            }
        }
        for(int fd : to_close) {
            close_connection(fd);
        }
    }
}

void HttpServer::accept_connection() {
    while(true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if(client_fd < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            log("ERROR", "Accept failed: " + std::string(strerror(errno)));
            break;
        }

        int flags = fcntl(client_fd, F_GETFL, 0);
        if(flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            log("ERROR", "Failed to set client socket non-blocking");
            close(client_fd);
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if(epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            log("ERROR", "Failed to add client to epoll");
            close(client_fd);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_[client_fd] = Connection{
                client_fd,
                std::string(client_ip),
                std::chrono::steady_clock::now(),
                config_.enable_keep_alive,
                "",
                0
            };
        }
        stats_.active_connections.fetch_add(1);
        on_connection_accepted(client_fd, client_ip);
    }
}

void HttpServer::handle_client_data(int client_fd) {
    thread_pool_->enqueue([this, client_fd]() {
        std::string client_ip;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(client_fd);
            if (it == connections_.end()) {
                return;
            }
            client_ip = it->second.ip;
            it->second.last_activity = std::chrono::steady_clock::now();
        }
        
        handle_request(client_fd, client_ip);
    });
}

void HttpServer::close_connection(int client_fd) {
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(client_fd);
        if (it != connections_.end()) {
            connections_.erase(it);
        }
    }
    
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    stats_.active_connections.fetch_sub(1);
    on_connection_closed(client_fd);
}

void HttpServer::handle_request(int client_fd, const std::string& client_ip) {
    try {
        HttpRequest request;
        request.set_client_ip(client_ip);

        if(!parse_request(client_fd, request)) {
            send_error_response(client_fd, HttpStatus::BAD_REQUEST);
            close_connection(client_fd);
            return;
        }
        stats_.total_requests.fetch_add(1);

        HttpResponse response;
        response.set_header("Server", config_.server_name);
        response.set_header("Date", get_current_time_string());

        // 执行全局中间件
        bool continue_processing = true;
        for(auto& middleware : global_middlewares_) {
            if(!middleware(request, response)) {
                continue_processing = false;
                break;
            }
        }
        if(continue_processing) {
            Route* matched_route = nullptr;
            std::smatch matches;
            if(match_route(request, matched_route, matches)) {
                for(size_t i = 1; i < matches.size(); ++i) {
                    if(i - 1 < matched_route->param_names.size()) {
                        request.set_path_param(matched_route->param_names[i-1], matches[i].str());
                    }
                }
                // 执行路由中间件
                execute_middlewares(matched_route->middlewares, request, response);
                matched_route->handler(request, response);
            }
            else {
                handle_static_file(request, response);
                if(response.status() == HttpStatus::OK) {
                    ;
                }
                else {
                    auto it = error_handlers_.find(404);
                    if(it != error_handlers_.end()) {
                        it->second(request, response, 404);
                    }
                    else {
                        default_error_handler_(request, response, 404);
                    }
                }
            }
        }
        // 设置 Keep-Alive 头
        if(config_.enable_keep_alive && request.get_header("Connection") != "close") {
            response.set_header("Connection", "keep-alive");
            response.set_header("Keep-Alive", "timeout=" + std::to_string(config_.keep_alive_timeout));
        }
        else {
            response.set_header("Connection", "close");
        }
        send_response(client_fd, response);
        stats_.total_responses.fetch_add(1);
        if (config_.enable_logging) {
            log_request(request, response);
        }
        if (!config_.enable_keep_alive || request.get_header("Connection") == "close" ||
            response.get_header("Connection") == "close") {
            close_connection(client_fd);
        }
    }
    catch(const std::exception& e) {
        log("ERROR", "Exception in handle_request: " + std::string(e.what()));
        send_error_response(client_fd, HttpStatus::INTERNAL_SERVER_ERROR);
        close_connection(client_fd);
    }
}

void HttpServer::get(const std::string& path, RouteHandler handler) {
    route(HttpMethod::GET, path, handler);
}

void HttpServer::post(const std::string& path, RouteHandler handler) {
    route(HttpMethod::POST, path, handler);
}

void HttpServer::put(const std::string& path, RouteHandler handler) {
    route(HttpMethod::PUT, path, handler);
}

void HttpServer::delete_(const std::string& path, RouteHandler handler) {
    route(HttpMethod::DELETE, path, handler);
}

void HttpServer::patch(const std::string& path, RouteHandler handler) {
    route(HttpMethod::PATCH, path, handler);
}

void HttpServer::options(const std::string& path, RouteHandler handler) {
    route(HttpMethod::OPTIONS, path, handler);
}

void HttpServer::head(const std::string& path, RouteHandler handler) {
    route(HttpMethod::HEAD, path, handler);
}

void HttpServer::route(HttpMethod method, const std::string& path, RouteHandler handler) {
    routes_.push_back(std::make_unique<Route>(method, path, std::move(handler)));
}

void HttpServer::use(MiddlewareFunc middleware) {
    global_middlewares_.push_back(std::move(middleware));
}

void HttpServer::use(const std::string& path, MiddlewareFunc middleware) {
    path_middlewares_[path].push_back(std::move(middleware));
}

void HttpServer::static_files(const std::string& url_path, const std::string& root_dir) {
    static_paths_[url_path] = root_dir;
}

void HttpServer::set_error_handler(int status_code, ErrorHandler handler) {
    error_handlers_[status_code] = std::move(handler);
}

void HttpServer::set_default_error_handler(ErrorHandler handler) {
    default_error_handler_ = std::move(handler);
}

bool HttpServer::parse_request(int client_fd, HttpRequest& request) {
    try {
        std::string request_line = read_request_line(client_fd);
        if(request_line.empty()) {
            return false;
        }
        std::istringstream iss(request_line);
        std::string method, path, version;
        if(!(iss >> method >> path >> version)) {
            return false;
        }
        request.set_method(method);

        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            request.set_query_string(path.substr(query_pos + 1));
            path = path.substr(0, query_pos);
        }
        request.set_path(url_decode(path));
        request.set_version(version);

        std::string headers_str = read_headers(client_fd);
        std::istringstream header_stream(headers_str);
        std::string header_line;
        while (std::getline(header_stream, header_line) && !header_line.empty()) {
            if (!header_line.empty() && header_line.back() == '\r') {
                header_line.pop_back();
            }
            size_t colon_pos = header_line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = header_line.substr(0, colon_pos);
                std::string value = header_line.substr(colon_pos + 1);
                // 去除前后空格
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                request.add_header(key, value);
            }
        }
        std::string content_length_str = request.get_header("Content-Length");
        if(!content_length_str.empty()) {
            size_t content_length = std::stoull(content_length_str);
            // 请求体过大
            if(content_length > config_.max_request_size) {
                return false; 
            }
            if(content_length > 0) {
                std::string body = read_body(client_fd, content_length);
                request.set_body(body);
                stats_.total_bytes_received.fetch_add(body.size());
            }
        }
        return true;
    }
    catch(const std::exception& e) {
        log("ERROR", "Failed to parse request: " + std::string(e.what()));
        return false;
    }
}

void HttpServer::send_response(int client_fd, const HttpResponse& response) {
    std::string response_str = response.to_string();
    ssize_t bytes_sent = write_to_socket(client_fd, response_str.c_str(), response_str.size());
    
    if (bytes_sent > 0) {
        stats_.total_bytes_sent.fetch_add(bytes_sent);
    }
}

bool HttpServer::match_route(const HttpRequest& request, Route*& matched_route, std::smatch& matches) {
    HttpMethod method = string_to_method(request.method());
    for (auto& route : routes_) {
        if (route->method == method) {
            if (std::regex_match(request.path(), matches, route->pattern)) {
                matched_route = route.get();
                return true;
            }
        }
    }
    return false;
}

void HttpServer::execute_middlewares(const std::vector<MiddlewareFunc>& middlewares,
                                   const HttpRequest& request, HttpResponse& response) {
    for (auto& middleware : middlewares) {
        if (!middleware(request, response)) {
            break;  // 中间件返回false，停止处理
        }
    }
}

void HttpServer::handle_static_file(const HttpRequest& request, HttpResponse& response) {
    std::string path = request.path();
    // 查找匹配的静态文件路径
    for (auto& [url_path, root_dir] : static_paths_) {
        if (path.substr(0, url_path.length()) == url_path) {
            std::string file_path = root_dir + path.substr(url_path.length());
            // 安全检查：防止目录遍历攻击
            if (!is_valid_path(file_path)) {
                response.set_status(HttpStatus::FORBIDDEN);
                return;
            }
            // 检查文件是否存在
            struct stat file_stat;
            if (stat(file_path.c_str(), &file_stat) != 0) {
                response.set_status(HttpStatus::NOT_FOUND);
                return;
            }
            // 如果是目录，尝试查找index文件
            if (S_ISDIR(file_stat.st_mode)) {
                file_path += "/index.html";
                if (stat(file_path.c_str(), &file_stat) != 0) {
                    response.set_status(HttpStatus::NOT_FOUND);
                    return;
                }
            }
            // 读取文件内容
            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open()) {
                response.set_status(HttpStatus::INTERNAL_SERVER_ERROR);
                return;
            }
            std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
            response.set_status(HttpStatus::OK);
            response.set_header("Content-Type", get_mime_type(file_path));
            response.set_header("Content-Length", std::to_string(content.size()));
            response.set_body(content);
            return;
        }
    }
    response.set_status(HttpStatus::NOT_FOUND);
}

void HttpServer::send_error_response(int client_fd, HttpStatus status, const std::string& message) {
    HttpResponse response;
    response.set_status(status);
    response.set_header("Content-Type", "text/html; charset=utf-8");
    response.set_header("Server", config_.server_name);
    
    std::string status_text = status_to_string(status);
    std::string body = message.empty() ? status_text : message;
    
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><title>Error " << static_cast<int>(status) 
         << "</title></head><body>";
    html << "<h1>Error " << static_cast<int>(status) << "</h1>";
    html << "<p>" << body << "</p>";
    html << "<hr><p>" << config_.server_name << "</p>";
    html << "</body></html>";
    
    response.set_body(html.str());
    send_response(client_fd, response);
}

// 工具方法实现
HttpMethod HttpServer::string_to_method(const std::string& method) {
    if (method == "GET") return HttpMethod::GET;
    if (method == "POST") return HttpMethod::POST;
    if (method == "PUT") return HttpMethod::PUT;
    if (method == "DELETE") return HttpMethod::DELETE;
    if (method == "PATCH") return HttpMethod::PATCH;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    if (method == "HEAD") return HttpMethod::HEAD;
    if (method == "TRACE") return HttpMethod::TRACE;
    if (method == "CONNECT") return HttpMethod::CONNECT;
    
    throw std::invalid_argument("Unknown HTTP method: " + method);
}

std::string HttpServer::method_to_string(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::PATCH: return "PATCH";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::TRACE: return "TRACE";
        case HttpMethod::CONNECT: return "CONNECT";
        default: return "UNKNOWN";
    }
}

std::string HttpServer::status_to_string(HttpStatus status) {
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

std::string HttpServer::get_mime_type(const std::string& file_path) {
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

bool HttpServer::is_valid_path(const std::string& path) {
    // 检查是否包含危险的路径遍历字符
    if (path.find("..") != std::string::npos) {
        return false;
    }
    // 检查是否为绝对路径
    if (path.empty() || path[0] != '/') {
        return false;
    }
    return true;
}

std::string HttpServer::url_decode(const std::string& encoded) {
    std::string decoded;
    decoded.reserve(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            // 解码百分号编码
            std::string hex = encoded.substr(i + 1, 2);
            try {
                int value = std::stoi(hex, nullptr, 16);
                decoded.push_back(static_cast<char>(value));
                i += 2;
            } catch (...) {
                decoded.push_back(encoded[i]);
            }
        } else if (encoded[i] == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(encoded[i]);
        }
    }
    return decoded;
}

ssize_t HttpServer::read_from_socket(int fd, char* buffer, size_t size) {
    ssize_t total_read = 0;
    ssize_t bytes_read;
    while (total_read < static_cast<ssize_t>(size)) {
        bytes_read = recv(fd, buffer + total_read, size - total_read, 0);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞模式下没有数据可读
                break;
            }
            return -1;  // 错误
        } else if (bytes_read == 0) {
            break;  // 连接关闭
        }
        
        total_read += bytes_read;
    }
    return total_read;
}

ssize_t HttpServer::write_to_socket(int fd, const char* data, size_t size) {
    ssize_t total_written = 0;
    ssize_t bytes_written;
    while (total_written < static_cast<ssize_t>(size)) {
        bytes_written = send(fd, data + total_written, size - total_written, MSG_NOSIGNAL);
        if (bytes_written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 暂时无法写入，等待下次
                break;
            }
            return -1;  // 错误
        } else if (bytes_written == 0) {
            break;  // 连接关闭
        }
        total_written += bytes_written;
    }
    return total_written;
}

std::string HttpServer::read_request_line(int client_fd) {
    std::string line;
    char ch;
    
    while (true) {
        ssize_t result = recv(client_fd, &ch, 1, 0);
        if (result <= 0) {
            break;
        }
        
        if (ch == '\r') {
            // 检查下一个字符是否为\n
            result = recv(client_fd, &ch, 1, MSG_PEEK);
            if (result > 0 && ch == '\n') {
                recv(client_fd, &ch, 1, 0);  // 消费\n
            }
            break;
        } else if (ch == '\n') {
            break;
        }
        
        line.push_back(ch);
        
        // 防止请求行过长
        if (line.size() > config_.max_header_size) {
            break;
        }
    }
    
    return line;
}

std::string HttpServer::read_headers(int client_fd) {
    std::string headers;
    std::string line;
    char ch;
    
    while (true) {
        line.clear();
        
        // 读取一行
        while (true) {
            ssize_t result = recv(client_fd, &ch, 1, 0);
            if (result <= 0) {
                return headers;
            }
            
            if (ch == '\r') {
                result = recv(client_fd, &ch, 1, MSG_PEEK);
                if (result > 0 && ch == '\n') {
                    recv(client_fd, &ch, 1, 0);
                }
                break;
            } else if (ch == '\n') {
                break;
            }
            
            line.push_back(ch);
        }
        
        // 空行表示头部结束
        if (line.empty()) {
            break;
        }
        
        headers += line + "\n";
        
        // 防止头部过长
        if (headers.size() > config_.max_header_size) {
            break;
        }
    }
    
    return headers;
}

std::string HttpServer::read_body(int client_fd, size_t content_length) {
    std::string body;
    body.reserve(content_length);
    
    char buffer[8192];
    size_t total_read = 0;
    
    while (total_read < content_length) {
        size_t to_read = std::min(sizeof(buffer), content_length - total_read);
        ssize_t bytes_read = recv(client_fd, buffer, to_read, 0);
        
        if (bytes_read <= 0) {
            break;
        }
        
        body.append(buffer, bytes_read);
        total_read += bytes_read;
    }
    
    return body;
}

void HttpServer::signal_handler(int signal) {
    if (instance_) {
        instance_->log("INFO", "Received signal " + std::to_string(signal) + ", shutting down");
        instance_->stop();
    }
}

void HttpServer::setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGINT, &sa, nullptr);   // Ctrl+C
    sigaction(SIGTERM, &sa, nullptr);  // Termination signal
    
    // 忽略SIGPIPE信号
    signal(SIGPIPE, SIG_IGN);
}

void HttpServer::log(const std::string& level, const std::string& message) {
    if (!config_.enable_logging) {
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    oss << "[" << level << "] " << message;
    
    std::cout << oss.str() << std::endl;
}

void HttpServer::log_request(const HttpRequest& request, const HttpResponse& response) {
    std::ostringstream oss;
    oss << request.client_ip() << " \"" << request.method() << " " << request.path();
    if (!request.query_string().empty()) {
        oss << "?" << request.query_string();
    }
    oss << " " << request.version() << "\" " << static_cast<int>(response.status());
    oss << " " << response.get_header("Content-Length");
    
    std::string user_agent = request.get_header("User-Agent");
    if (!user_agent.empty()) {
        oss << " \"" << user_agent << "\"";
    }
    
    log("INFO", oss.str());
}

std::string HttpServer::get_current_time_string() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%a, %d %b %Y %H:%M:%S GMT");
    return oss.str();
}

// 虚函数的默认实现
void HttpServer::on_connection_accepted(int client_fd, const std::string& client_ip) {
    // 子类可以重写此方法来处理新连接
}

void HttpServer::on_connection_closed(int client_fd) {
    // 子类可以重写此方法来处理连接关闭
}

void HttpServer::on_error(const std::string& error_message) {
    log("ERROR", error_message);
}