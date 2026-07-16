/**
 * desktop/src/ui/qt_main.cpp — Qt GUI 入口
 *
 * 启动 DesktopAPI + MainWindow, 进入 Qt 事件循环。
 */
#include <QApplication>
#include <QDebug>
#include <QDir>

#include "api/desktop_api.h"
#include "main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Nikon Camera Connect");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("NikonConnect");

    /* 全局字体 */
    QFont font("SF Pro Display", 13);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    /* 创建 API */
    DesktopAPI api(TRANSPORT_AUTO);

    /* 创建主窗口 */
    MainWindow window(&api);
    window.setWindowTitle("Nikon Camera Connect");
    window.resize(1100, 720);
    window.setMinimumSize(900, 600);
    window.show();

    int ret = app.exec();

    /* 确保 API 先清理 */
    api.shutdown();

    qDebug() << "Nikon Camera Connect exited with code" << ret;
    return ret;
}
