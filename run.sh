#!/bin/bash

# OJ System Run Script
set -e

BUILD_DIR="build"
EXECUTABLE="$BUILD_DIR/oj_server"

# 检查是否已构建
if [ ! -f "$EXECUTABLE" ]; then
    echo "Project not built. Running build script..."
    ./build.sh
fi

# 设置环境变量
export OJ_CONFIG_PATH="./config"
export OJ_LOG_PATH="./logs"
export OJ_UPLOAD_PATH="./uploads"
export OJ_PUBLIC_PATH="./public"

# 创建默认配置文件（如果不存在）
if [ ! -f "config/server.json" ]; then
    echo "Creating default configuration..."
    mkdir -p config
    cat > config/server.json << 'EOF'
{
    "server": {
        "host": "0.0.0.0",
        "port": 9006,
        "thread_pool_size": 8,
        "enable_logging": true,
        "log_level": "INFO"
    },
    "database": {
        "type": "sqlite",
        "path": "data/oj.db"
    },
    "judge": {
        "enabled": true,
        "sandbox_path": "./judge/sandbox",
        "time_limit": 1000,
        "memory_limit": 128
    }
}
EOF
fi

# 启动服务器
echo "Starting OJ System..."
echo "Server will be available at http://localhost:9006"
echo "Press Ctrl+C to stop the server"

"$EXECUTABLE"