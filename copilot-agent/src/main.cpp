#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("CopilotAgent"));
    QCoreApplication::setApplicationName(QStringLiteral("Copilot Agent"));
    QApplication app(argc, argv);

    MainWindow w;
    w.show();
    return app.exec();
}
