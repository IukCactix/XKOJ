#!/bin/bash

# OJ System Build Script
set -e

PROJECT_NAME="XKOJ"
BUILD_DIR="build"
INSTALL_DIR="install"

echo "Building $PROJECT_NAME..."

# 创建构建目录
if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

# 创建必要的运行时目录
mkdir -p logs
mkdir -p uploads/{problems,submissions,avatars}
mkdir -p public/{css,js,images}
mkdir -p judge/{testcases,sandbox}

# 检查依赖
echo "Checking dependencies..."
if ! command -v cmake &> /dev/null; then
    echo "Error: cmake is required but not installed."
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo "Error: g++ is required but not installed."
    exit 1
fi

# 构建项目
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="../$INSTALL_DIR"
make -j$(nproc)

echo "Build completed successfully!"
echo "Run './run.sh' to start the server."
