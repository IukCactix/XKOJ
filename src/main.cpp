#include "core/http_server.h"
#include "core/http_request.h"
#include "core/http_response.h"
#include "core/middleware.h"
#include "core/config_manager.h"
#include "core/logger.h"
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
    try {
        // 初始化配置管理
        std::string config_path = "config/server.json";
        if (argc > 1) {
            config_path = argv[1];
        }
        
        auto& config = ConfigManager::instance();
        if (!config.load_config(config_path)) {
            std::cerr << "Failed to load configuration file: " << config_path << std::endl;
            return 1;
        }
        
        // 初始化日志系统
        std::string log_file = config.get<std::string>("server.log_file", "logs/server.log");
        std::string log_level_str = config.get<std::string>("server.log_level", "INFO");
        
        LogLevel log_level = LogLevel::INFO;
        if (log_level_str == "DEBUG") log_level = LogLevel::DEBUG;
        else if (log_level_str == "WARN") log_level = LogLevel::WARN;
        else if (log_level_str == "ERROR") log_level = LogLevel::ERROR;
        else if (log_level_str == "FATAL") log_level = LogLevel::FATAL;
        
        // 确保日志目录存在
        std::filesystem::create_directories(std::filesystem::path(log_file).parent_path());
        
        Logger::instance().init(log_file, log_level);
        LOG_INFO("OJ System starting...");
        
        // 配置服务器
        HttpServer::ServerConfig server_config;
        server_config.host = config.get<std::string>("server.host", "0.0.0.0");
        server_config.port = config.get<int>("server.port", 8080);
        server_config.thread_pool_size = config.get<int>("server.thread_pool_size", 8);
        server_config.enable_logging = config.get<bool>("server.enable_logging", true);
        server_config.enable_keep_alive = config.get<bool>("server.enable_keep_alive", true);
        server_config.timeout_seconds = config.get<int>("server.timeout_seconds", 30);
        
        HttpServer server(server_config);
        
        // 添加中间件
        server.use(Middleware::create(std::make_shared<LoggingMiddleware>()));
        
        // 静态文件服务
        std::string public_path = config.get<std::string>("server.public_path", "./public");
        server.static_files("/", public_path);
        server.static_files("/static", public_path);
        
        // 基础路由
        server.get("/", [](const HttpRequest& req, HttpResponse& res) {
            res.html(R"(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>OJ System</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .header { text-align: center; margin-bottom: 30px; }
        .nav { display: flex; justify-content: center; gap: 20px; margin-bottom: 30px; }
        .nav a { text-decoration: none; color: #007bff; padding: 10px 20px; border: 1px solid #007bff; border-radius: 4px; }
        .nav a:hover { background: #007bff; color: white; }
        .status { background: #d4edda; padding: 15px; border-radius: 4px; margin-bottom: 20px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🏆 OJ System</h1>
            <p>在线评测系统</p>
        </div>
        
        <div class="status">
            <strong>✅ 系统状态:</strong> 运行正常
        </div>
        
        <div class="nav">
            <a href="/api/health">健康检查</a>
            <a href="/api/problems">题目列表</a>
            <a href="/api/status">系统状态</a>
            <a href="/api/docs">API文档</a>
        </div>
        
        <div>
            <h2>功能模块</h2>
            <ul>
                <li>✅ HTTP服务器 - 完成</li>
                <li>✅ 中间件系统 - 完成</li>
                <li>✅ 配置管理 - 完成</li>
                <li>✅ 日志系统 - 完成</li>
                <li>🔨 用户管理 - 开发中</li>
                <li>🔨 题目管理 - 开发中</li>
                <li>🔨 评测系统 - 开发中</li>
                <li>🔨 比赛系统 - 开发中</li>
            </ul>
        </div>
    </div>
</body>
</html>
            )");
        });
        
        // API路由
        server.get("/api/health", [](const HttpRequest& req, HttpResponse& res) {
            auto& config = ConfigManager::instance();
            res.json(R"({
                "status": "ok",
                "message": "OJ System is running",
                "version": "1.0.0",
                "timestamp": ")" + std::to_string(std::time(nullptr)) + R"(",
                "server": {
                    "host": ")" + config.get<std::string>("server.host", "0.0.0.0") + R"(",
                    "port": )" + std::to_string(config.get<int>("server.port", 8080)) + R"(
                }
            })");
        });
        
        server.get("/api/status", [&server](const HttpRequest& req, HttpResponse& res) {
            const auto& stats = server.stats();
            res.json(R"({
                "statistics": {
                    "total_requests": )" + std::to_string(stats.total_requests.load()) + R"(,
                    "total_responses": )" + std::to_string(stats.total_responses.load()) + R"(,
                    "active_connections": )" + std::to_string(stats.active_connections.load()) + R"(,
                    "bytes_sent": )" + std::to_string(stats.total_bytes_sent.load()) + R"(,
                    "bytes_received": )" + std::to_string(stats.total_bytes_received.load()) + R"(
                }
            })");
        });
        
        server.get("/api/problems", [](const HttpRequest& req, HttpResponse& res) {
            res.json(R"({
                "problems": [
                    {
                        "id": 1,
                        "title": "Hello World",
                        "difficulty": "Easy",
                        "tags": ["入门", "输出"],
                        "accepted": 1250,
                        "submitted": 1500
                    },
                    {
                        "id": 2,
                        "title": "A+B Problem",
                        "difficulty": "Easy", 
                        "tags": ["数学", "入门"],
                        "accepted": 980,
                        "submitted": 1200
                    },
                    {
                        "id": 3,
                        "title": "排序算法",
                        "difficulty": "Medium",
                        "tags": ["排序", "算法"],
                        "accepted": 450,
                        "submitted": 890
                    }
                ],
                "total": 3
            })");
        });
        
        server.get("/api/docs", [](const HttpRequest& req, HttpResponse& res) {
            res.html(R"(
<!DOCTYPE html>
<html>
<head>
    <title>API Documentation</title>
    <style>
        body { font-family: monospace; margin: 20px; }
        .endpoint { margin: 20px 0; padding: 15px; border: 1px solid #ddd; }
        .method { background: #007bff; color: white; padding: 2px 8px; border-radius: 3px; }
        .path { font-weight: bold; }
    </style>
</head>
<body>
    <h1>API Documentation</h1>
    
    <div class="endpoint">
        <div><span class="method">GET</span> <span class="path">/api/health</span></div>
        <div>健康检查接口</div>
    </div>
    
    <div class="endpoint">
        <div><span class="method">GET</span> <span class="path">/api/status</span></div>
        <div>系统状态统计</div>
    </div>
    
    <div class="endpoint">
        <div><span class="method">GET</span> <span class="path">/api/problems</span></div>
        <div>获取题目列表</div>
    </div>
    
    <div class="endpoint">
        <div><span class="method">GET</span> <span class="path">/api/problems/:id</span></div>
        <div>获取指定题目详情</div>
    </div>
</body>
</html>
            )");
        });
        
        // 启动服务器
        if (!server.start()) {
            LOG_ERROR("Failed to start HTTP server");
            return 1;
        }
        
        LOG_INFO("OJ System started successfully");
        LOG_INFO("Server listening on http://" + server_config.host + ":" + std::to_string(server_config.port));
        
        // 等待服务器运行
        while (server.is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        LOG_INFO("OJ System shutting down");
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        LOG_FATAL("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}