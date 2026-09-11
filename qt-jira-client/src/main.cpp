#include "core/jiraclient.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QNetworkProxyFactory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("JiraDesk"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setOrganizationName(QStringLiteral("JiraDesk"));
    QApplication::setOrganizationDomain(QStringLiteral("jiradesk.local"));

    // Corporate networks reach Jira through a proxy far more often than not.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(
            QApplication::translate("main",
                                    "A Qt desktop client for Jira over the REST API v2. Point it at any "
                                    "Jira — Server, Data Center or Cloud — and authenticate with an API token."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption queryOption({QStringLiteral("q"), QStringLiteral("jql")},
                                   QApplication::translate("main", "Run this JQL query at start-up."),
                                   QStringLiteral("jql"));
    parser.addOption(queryOption);
    parser.process(application);

    auto *client = new jira::Client(&application);
    MainWindow window(client);
    if (parser.isSet(queryOption))
        window.setInitialQuery(parser.value(queryOption));
    window.show();
    window.start();

    return QApplication::exec();
}
