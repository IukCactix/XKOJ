#include <functional>
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>
#include <regex>

// 前向声明
class HttpRequest;
class HttpResponse;
class Middleware;

// HTTP方法枚举
enum class HttpMethod {
    GET, POST, PUT, DELETE, PATCH, OPTIONS, HEAD
};

using RouteHandler = std::function<void(HttpRequest&, HttpResponse&)>;

//路由信息
struct Route {
    std::string method;         // GET, POST等
    std::regex path_pattern;    // URL路径
    RouteHandler handler;       // 处理函数
    
    Route(const std::string& m, const std::string& path, RouteHandler h) 
        : method(m), handler(h){
        std::string pattern = path;
        pattern = std::regex_replace(pattern, std::regex(":([a-zA-Z_][a-zA-Z0-9_]*)"), "([^/]+)");
        path_pattern = std::regex(pattern);
    }
};

class HttpServer {
public:
    HttpServer(int port):port_(port), server_fd_(-1), running_(false) {}

    bool start() {
        // 1. 创建socket
        // 2. 绑定端口
        // 3. 监听
        // 4. 循环接受连接
        return true;
    }

    void stop() {
        running_ = false;
        // 关闭socket
    }

    void get(const std::string& path, RouteHandler handler) {
        routes_.push_back({"GET", path, handler});
    }
    void post(const std::string& path, RouteHandler handler) {
        routes_.push_back({"POST", path, handler});
    }
private:
    void handle_client(int client_fd) {
        //读取客户端发送的数据
        std::string raw_request = read_from_socket(client_fd);
        //解析成HttpRequest对象
        HttpRequest request;
        if(!request.parse(raw_request)) {
            send_error_response(cient_fd, 400);
            return ;
        }
        //查找匹配的路由
        Route* matched_route = find_route(request.method(), request.path());
        if(!matched_route) {
            send_error_response(client_fd, 404);
            return ;
        }
        //执行处理函数
        HttpResponse response;
        matched_route->handler(request, response);
        //发送响应
        send_response(client_fd, response);
    }

    Route* find_route(const std::string& method, const std::string& path, std::smatch& matches) {
        for(auto& route : routes_) {
            if(route.method == method && std::regex_match(path, matches, route.path_pattern)) {
                return &route;
            }
        }
        return nullptr;
    }

private:
    int port_;
    int server_fd_;
    bool running_;
    std::vector<Route> routes_;
};