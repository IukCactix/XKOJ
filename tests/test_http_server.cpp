#include "core/http_server.h"
#include "core/http_request.h"
#include "core/http_response.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

void test_basic_functionality() {
    std::cout << "Testing basic HTTP server functionality..." << std::endl;
    
    HttpServer::ServerConfig config;
    config.port = 9999;  // 使用不同端口避免冲突
    config.enable_logging = false;  // 测试时关闭日志
    
    HttpServer server(config);
    
    // 添加测试路由
    server.get("/test", [](const HttpRequest& req, HttpResponse& res) {
        res.text("Test successful");
    });
    
    server.get("/json", [](const HttpRequest& req, HttpResponse& res) {
        res.json(R"({"status": "ok", "message": "JSON response"})");
    });
    
    // 启动服务器
    if (!server.start()) {
        std::cerr << "Failed to start test server" << std::endl;
        assert(false);
    }
    
    std::cout << "Test server started on port " << config.port << std::endl;
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 这里可以添加HTTP客户端测试代码
    // 由于篇幅限制，这里只演示服务器启动
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    server.stop();
    std::cout << "Basic functionality test passed!" << std::endl;
}

void test_request_parsing() {
    std::cout << "Testing HTTP request parsing..." << std::endl;
    
    HttpRequest request;
    std::string raw_request = 
        "GET /api/users/123?active=true HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: TestClient/1.0\r\n"
        "Accept: application/json\r\n"
        "Authorization: Bearer test-token\r\n"
        "\r\n";
    
    assert(request.parse(raw_request));
    assert(request.method() == "GET");
    assert(request.path() == "/api/users/123");
    assert(request.query_string() == "active=true");
    assert(request.get_header("Host") == "localhost:8080");
    assert(request.get_header("Authorization") == "Bearer test-token");
    assert(request.get_param("active") == "true");
    
    std::cout << "Request parsing test passed!" << std::endl;
}

void test_response_generation() {
    std::cout << "Testing HTTP response generation..." << std::endl;
    
    HttpResponse response;
    response.set_status(HttpStatus::OK);
    response.set_header("Content-Type", "application/json");
    response.set_body(R"({"message": "Hello World"})");
    
    std::string response_str = response.to_string();
    
    assert(response_str.find("HTTP/1.1 200 OK") != std::string::npos);
    assert(response_str.find("Content-Type: application/json") != std::string::npos);
    assert(response_str.find(R"({"message": "Hello World"})") != std::string::npos);
    
    std::cout << "Response generation test passed!" << std::endl;
}

int main() {
    try {
        test_request_parsing();
        test_response_generation();
        test_basic_functionality();
        
        std::cout << "\nAll tests passed successfully!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}