#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <functional>
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>
#include <regex>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <signal.h>

class HttpRequest;
class HttpResponse;

enum class HttpMethod {
    GET, POST, PUT, DELETE, PATCH, OPTIONS, HEAD, TRACE, CONNECT
};

// HTTP状态码枚举
enum class HttpStatus {
    // 1xx Informational
    CONTINUE = 100,
    SWITCHING_PROTOCOLS = 101,
    
    // 2xx Success
    OK = 200,
    CREATED = 201,
    ACCEPTED = 202,
    NO_CONTENT = 204,
    RESET_CONTENT = 205,
    PARTIAL_CONTENT = 206,
    
    // 3xx Redirection
    MULTIPLE_CHOICES = 300,
    MOVED_PERMANENTLY = 301,
    FOUND = 302,
    SEE_OTHER = 303,
    NOT_MODIFIED = 304,
    TEMPORARY_REDIRECT = 307,
    PERMANENT_REDIRECT = 308,
    
    // 4xx Client Error
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    PAYMENT_REQUIRED = 402,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    NOT_ACCEPTABLE = 406,
    REQUEST_TIMEOUT = 408,
    CONFLICT = 409,
    GONE = 410,
    LENGTH_REQUIRED = 411,
    PAYLOAD_TOO_LARGE = 413,
    URI_TOO_LONG = 414,
    UNSUPPORTED_MEDIA_TYPE = 415,
    RANGE_NOT_SATISFIABLE = 416,
    EXPECTATION_FAILED = 417,
    UNPROCESSABLE_ENTITY = 422,
    TOO_MANY_REQUESTS = 429,
    
    // 5xx Server Error
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503,
    GATEWAY_TIMEOUT = 504,
    HTTP_VERSION_NOT_SUPPORTED = 505
};

using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;
using MiddlewareFunc = std::function<bool(const HttpRequest&, HttpResponse&)>;
using ErrorHandler = std::function<void(const HttpRequest&, HttpResponse&, int error_code)>;

// 路由信息结构
struct Route {
    HttpMethod method;
    std::regex pattern;
    std::string original_path;
    RouteHandler handler;
    std::vector<MiddlewareFunc> middlewares;
    std::vector<std::string> param_names;

    Route(HttpMethod m, const std::string& path, RouteHandler h);

private:
    void compile_path(const std::string& path);
};

// 线程池任务
struct Task {
    std::function<void()> function;
    Task(std::function<void()> f) : function(std::move(f)) {}
};

// 线程池
class ThreadPool {
public:
    ThreadPool(size_t num_threads);
    ~ThreadPool();

    template<typename F>
    void enqueue(F&& f);

    void shutdown();

private:
    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;

    void worker_thread();
};

class HttpServer {
public:
    struct ServerConfig {
        int port = 9006;
        std::string host = "0.0.0.0";
        int thread_pool_size;
        int max_connections = 1000;
        int timeout_seconds = 30;
        int keep_alive_timeout = 5;
        bool enable_keep_alive = true;
        bool enable_compression = true;
        size_t max_request_size = 1024 * 1024;  // 1MB
        size_t max_header_size = 8192;  // 8KB
        std::string server_name = "XKOJ/1.0";
        bool enable_cors = false;
        bool enable_logging = true;

        ServerConfig() : thread_pool_size(std::thread::hardware_concurrency()) {}
    };

    explicit HttpServer(const ServerConfig& config = ServerConfig{});
    virtual ~HttpServer();

    // 禁用拷贝构造和赋值
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // 启动和停止服务器
    bool start();
    void stop();
    bool is_running() const {return running_.load();}

    // 路由注册
    void get(const std::string& path, RouteHandler handler);
    void post(const std::string& path, RouteHandler handler);
    void put(const std::string& path, RouteHandler handler);
    void delete_(const std::string& path, RouteHandler handler);
    void patch(const std::string& path, RouteHandler handler);
    void options(const std::string& path, RouteHandler handler);
    void head(const std::string& path, RouteHandler handler);
    void route(HttpMethod method, const std::string& path, RouteHandler handler);

    // 中间件管理
    void use(MiddlewareFunc middleware);  // 全局中间件
    void use(const std::string& path, MiddlewareFunc middleware);  // 路径中间件

    // 静态文件服务
    void static_files(const std::string& url_path, const std::string& root_dir);

    // 错误处理
    void set_error_handler(int status_code, ErrorHandler handler);
    void set_default_error_handler(ErrorHandler handler);

    // 配置获取
    const ServerConfig& config() const { return config_; }

    // 统计信息
    struct Statistics {
        std::atomic<uint64_t> total_requests{0};
        std::atomic<uint64_t> total_responses{0};
        std::atomic<uint64_t> active_connections{0};
        std::atomic<uint64_t> total_bytes_sent{0};
        std::atomic<uint64_t> total_bytes_received{0};
        std::chrono::steady_clock::time_point start_time;
    };

protected:
    virtual void handle_request(int client_fd, const std::string& client_ip);
    virtual bool parse_request(int client_fd, HttpRequest& request);
    virtual void send_response(int client_fd, const HttpResponse& response);
    virtual void on_connection_accepted(int client_fd, const std::string& client_ip);
    virtual void on_connection_closed(int client_fd);
    virtual void on_error(const std::string& error_message);

private:
    ServerConfig config_;
    std::atomic<bool> running_;
    std::atomic<bool> shutting_down_;
    
    // 网络相关
    int server_fd_;
    int epoll_fd_;
    struct sockaddr_in server_addr_;
    
    // 线程池
    std::unique_ptr<ThreadPool> thread_pool_;
    
    // 路由和中间件
    std::vector<std::unique_ptr<Route>> routes_;
    std::vector<MiddlewareFunc> global_middlewares_;
    std::unordered_map<std::string, std::vector<MiddlewareFunc>> path_middlewares_;
    
    // 静态文件配置
    std::unordered_map<std::string, std::string> static_paths_;

    // 错误处理
    std::unordered_map<int, ErrorHandler> error_handlers_;
    ErrorHandler default_error_handler_;
    
    // 统计信息
    mutable Statistics stats_;

    // 连接管理
    struct Connection {
        int fd;
        std::string ip;
        std::chrono::steady_clock::time_point last_activity;
        bool keep_alive;
        std::string buffer;
        size_t bytes_read;
    };

    std::unordered_map<int, Connection> connections_;
    std::mutex connections_mutex_;
    
    // 主循环
    std::thread main_thread_;
    std::thread cleanup_thread_;

    // 网络相关私有方法
    bool create_socket();
    bool bind_socket();
    bool listen_socket();
    bool setup_epoll();
    void main_loop();
    void cleanup_loop();
    void accept_connection();
    void handle_client_data(int client_fd);
    void close_connection(int client_fd);
    
    // 请求处理相关
    bool match_route(const HttpRequest& request, Route*& matched_route, 
                    std::smatch& matches);
    void execute_middlewares(const std::vector<MiddlewareFunc>& middlewares,
                           const HttpRequest& request, HttpResponse& response);
    void handle_static_file(const HttpRequest& request, HttpResponse& response);
    void send_error_response(int client_fd, HttpStatus status, 
                           const std::string& message = "");
    
    // 工具方法
    HttpMethod string_to_method(const std::string& method);
    std::string method_to_string(HttpMethod method);
    std::string status_to_string(HttpStatus status);
    std::string get_mime_type(const std::string& file_path);
    bool is_valid_path(const std::string& path);
    std::string url_decode(const std::string& encoded);
    std::string get_current_time_string();
    
    // 数据读写
    ssize_t read_from_socket(int fd, char* buffer, size_t size);
    ssize_t write_to_socket(int fd, const char* data, size_t size);
    std::string read_request_line(int client_fd);
    std::string read_headers(int client_fd);
    std::string read_body(int client_fd, size_t content_length);
    
    // 信号处理
    static void signal_handler(int signal);
    static HttpServer* instance_;
    void setup_signal_handlers();
    
    // 日志记录
    void log(const std::string& level, const std::string& message);
    void log_request(const HttpRequest& request, const HttpResponse& response);
};

template<typename F>
void ThreadPool::enqueue(F&& f) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) {
            return;
        }
        tasks_.emplace(std::forward<F>(f));
    }
    condition_.notify_one();
}

#endif // HTTP_SERVER_H