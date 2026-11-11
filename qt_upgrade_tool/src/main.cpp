#include "mainwindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("STM32 Firmware Upgrade Tool"));
    app.setOrganizationName(QStringLiteral("STM32"));

    /* Fusion 在 Linux 各种桌面环境下表现一致 */
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    MainWindow w;
    w.show();
    return app.exec();
}
