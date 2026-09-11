#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>

/*!
 * \file main.cpp
 * \brief Starts the application.
 *
 * The organisation and application names are set before anything else because
 * QSettings and the standard data locations are derived from them; setting them
 * late would quietly split the settings and the database across two paths.
 */
int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("WorkCalendar"));
    QCoreApplication::setApplicationName(QStringLiteral("WorkCalendar"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/workcalendar.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Log your work, see the month at a glance, export the spreadsheet."));
    parser.addHelpOption();
    parser.addVersionOption();

    // A second database is useful for trying things out, and for keeping a
    // separate log per customer.
    QCommandLineOption databaseOption(
        QStringList() << QStringLiteral("d") << QStringLiteral("database"),
        QStringLiteral("Use the work log stored in <file> instead of the standard one."),
        QStringLiteral("file"));
    parser.addOption(databaseOption);
    parser.process(application);

    MainWindow window;
    if (!window.initialise(parser.value(databaseOption)))
        return 1;

    window.show();
    return application.exec();
}
