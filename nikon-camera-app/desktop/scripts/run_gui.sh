#!/bin/bash
# desktop/scripts/run_gui.sh — 启动 Nikon Camera Connect GUI
#
# 用法:
#   ./run_gui.sh              # Mock 模式 (演示 UI)
#   NIKON_MOCK=0 ./run_gui.sh # 真实模式 (需要 libcore.dylib)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYTHON="/Users/liutao/.workbuddy/binaries/python/envs/default/bin/python"

cd "$PROJECT_DIR/desktop/src/py_gui"

if [ ! -f "$PYTHON" ]; then
    echo "Error: Python venv not found at $PYTHON"
    echo "Run: /Users/liutao/.workbuddy/binaries/python/versions/3.13.12/bin/python3 -m venv /Users/liutao/.workbuddy/binaries/python/envs/default"
    echo "     /Users/liutao/.workbuddy/binaries/python/envs/default/bin/pip install PySide6"
    exit 1
fi

echo "=== Nikon Camera Connect GUI ==="
echo "Mode: ${NIKON_MOCK:+REAL}${NIKON_MOCK:-MOCK}"
echo ""

exec "$PYTHON" main.py "$@"
