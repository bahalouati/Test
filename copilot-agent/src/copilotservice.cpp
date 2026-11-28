#include "copilotservice.h"

#include <QStringList>
#include <QUrl>

namespace CopilotService {

QString defaultUrl(Kind k)
{
    switch (k) {
    case Work:     return QStringLiteral("https://m365.cloud.microsoft/chat");
    case Personal: return QStringLiteral("https://copilot.com/chat");
    case Custom:   return QString();
    }
    return QString();
}

Kind kindOfHost(const QString &hostIn)
{
    const QString host = hostIn.trimmed().toLower();
    static const QStringList work = {
        QStringLiteral("m365.cloud.microsoft"), QStringLiteral("copilot.cloud.microsoft"),
        QStringLiteral("www.microsoft365.com"), QStringLiteral("microsoft365.com"), QStringLiteral("www.office.com"),
        QStringLiteral("m365copilot.com"), QStringLiteral("www.m365copilot.com"),
    };
    static const QStringList personal = {
        QStringLiteral("copilot.com"), QStringLiteral("www.copilot.com"),
        QStringLiteral("copilot.microsoft.com"),
    };
    if (work.contains(host) || host.endsWith(QLatin1String(".cloud.microsoft")))
        return Work;
    if (personal.contains(host))
        return Personal;
    return Custom;
}

Kind kindOfUrl(const QString &url)
{
    QString u = url.trimmed();
    if (!u.contains(QLatin1String("://")))
        u.prepend(QLatin1String("https://"));
    return kindOfHost(QUrl(u).host());
}

bool sameService(const QString &host, const QString &startHost)
{
    if (host.isEmpty() || startHost.isEmpty())
        return false;
    if (host.compare(startHost, Qt::CaseInsensitive) == 0)
        return true;
    const Kind a = kindOfHost(host), b = kindOfHost(startHost);
    return a == b && a != Custom;
}

bool isSignInHost(const QString &hostIn, Kind k)
{
    const QString host = hostIn.toLower();
    if (k == Personal)
        return host == QLatin1String("login.live.com") || host == QLatin1String("account.live.com")
            || host == QLatin1String("login.microsoftonline.com");
    return host.contains(QLatin1String("login.microsoftonline"));
}

QString profileFolder(Kind k)
{
    // The work profile keeps the name it always had, so nobody signs in again.
    return k == Personal ? QStringLiteral("edge-profile-personal") : QStringLiteral("edge-profile");
}

} // namespace CopilotService
