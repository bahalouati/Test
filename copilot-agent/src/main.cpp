#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("CopilotAgent"));
    QCoreApplication::setApplicationName(QStringLiteral("Copilot Agent"));
    QApplication app(argc, argv);

    MainWindow w;
    w.show();
    // Files given on the command line (or dropped on the program's icon in
    // Explorer) become the picked files for the next task.
    const QStringList files = app.arguments().mid(1);
    if (!files.isEmpty())
        w.addFiles(files);
    return app.exec();
}
