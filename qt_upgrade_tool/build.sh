#!/usr/bin/env bash
# Ubuntu 18.04 一键构建
set -e

cd "$(dirname "$0")"

if ! dpkg -s qtbase5-dev >/dev/null 2>&1; then
    echo "缺少 Qt5 开发包，先执行："
    echo "  sudo apt update && sudo apt install -y build-essential cmake qtbase5-dev libqt5serialport5-dev"
    exit 1
fi

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"

echo
echo "构建完成： $(pwd)/fw_upgrade_tool"
echo "运行： ./fw_upgrade_tool"
