"""
desktop/src/py_gui/main.py — Nikon Camera Connect GUI 入口

启动 DesktopAPI + MainWindow，进入 Qt 事件循环。
用法: python main.py [--mock]
"""
import sys
import os

# 确保能找到页面模块
sys.path.insert(0, os.path.dirname(__file__))

from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QFont

from desktop_api import DesktopAPI, MOCK
from main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("Nikon Camera Connect")
    app.setApplicationVersion("1.0.0")
    app.setOrganizationName("NikonConnect")

    # 全局字体
    font = QFont("Helvetica Neue", 13)
    font.setStyleStrategy(QFont.PreferAntialias)
    app.setFont(font)

    # 创建 API
    api = DesktopAPI()

    # 创建主窗口
    window = MainWindow(api)
    window.setWindowTitle("Nikon Camera Connect")
    window.resize(1100, 720)
    window.setMinimumSize(900, 600)
    window.show()

    if MOCK:
        print("[INFO] 运行在 MOCK 模式 — 模拟相机数据用于 UI 演示")
        print("[INFO] 设置环境变量 NIKON_MOCK=0 启用真实相机模式")

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
