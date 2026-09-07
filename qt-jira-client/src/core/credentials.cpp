#include "credentials.h"

#include <QSettings>
#include <QUrl>

namespace jira {

namespace {
constexpr auto kBaseUrlKey = "connection/baseUrl";
constexpr auto kAuthModeKey = "connection/authMode";
constexpr auto kUsernameKey = "connection/username";
constexpr auto kTokenKey = "connection/token";
constexpr auto kRememberKey = "connection/rememberToken";
constexpr auto kCaCertKey = "connection/caCertificate";
constexpr auto kAllowInvalidKey = "connection/allowInvalidCertificates";
} // namespace

QString authModeToString(AuthMode mode)
{
    return mode == AuthMode::Bearer ? QStringLiteral("bearer") : QStringLiteral("basic");
}

AuthMode authModeFromString(const QString &text, AuthMode fallback)
{
    const QString normalised = text.trimmed().toLower();
    if (normalised == QLatin1String("bearer"))
        return AuthMode::Bearer;
    if (normalised == QLatin1String("basic"))
        return AuthMode::Basic;
    return fallback;
}

QString normalizeBaseUrl(const QString &raw)
{
    QString url = raw.trimmed();
    if (url.isEmpty())
        return url;

    if (!url.contains(QLatin1String("://")))
        url.prepend(QLatin1String("https://"));

    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);

    // Forgive pasting the REST root, or a URL copied out of the browser while
    // looking at an issue.
    static const QStringList strippableSuffixes = {
        QStringLiteral("/rest/api/2"),
        QStringLiteral("/rest/api/3"),
        QStringLiteral("/rest/api/latest"),
        QStringLiteral("/secure/Dashboard.jspa"),
    };
    for (const QString &suffix : strippableSuffixes) {
        if (url.endsWith(suffix, Qt::CaseInsensitive)) {
            url.chop(suffix.size());
            break;
        }
    }
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);

    return url;
}

bool Credentials::isComplete() const
{
    if (normalizeBaseUrl(baseUrl).isEmpty() || token.isEmpty())
        return false;
    if (mode == AuthMode::Basic && username.trimmed().isEmpty())
        return false;
    return QUrl(normalizeBaseUrl(baseUrl)).isValid();
}

QByteArray Credentials::authorizationHeader() const
{
    if (token.isEmpty())
        return {};

    if (mode == AuthMode::Bearer)
        return "Bearer " + token.trimmed().toUtf8();

    const QByteArray pair = username.trimmed().toUtf8() + ':' + token.trimmed().toUtf8();
    return "Basic " + pair.toBase64();
}

Credentials Credentials::load()
{
    QSettings settings;
    Credentials credentials;
    credentials.baseUrl = settings.value(QLatin1String(kBaseUrlKey)).toString();
    credentials.mode = authModeFromString(settings.value(QLatin1String(kAuthModeKey)).toString(),
                                          credentials.mode);
    credentials.username = settings.value(QLatin1String(kUsernameKey)).toString();
    credentials.rememberToken = settings.value(QLatin1String(kRememberKey), false).toBool();
    credentials.caCertificatePath = settings.value(QLatin1String(kCaCertKey)).toString();
    credentials.allowInvalidCertificates = settings.value(QLatin1String(kAllowInvalidKey), false).toBool();
    if (credentials.rememberToken)
        credentials.token = settings.value(QLatin1String(kTokenKey)).toString();

    const auto env = [](const char *name) { return QString::fromLocal8Bit(qgetenv(name)).trimmed(); };
    if (const QString value = env("JIRA_BASE_URL"); !value.isEmpty())
        credentials.baseUrl = value;
    if (const QString value = env("JIRA_USER"); !value.isEmpty())
        credentials.username = value;
    else if (const QString email = env("JIRA_EMAIL"); !email.isEmpty())
        credentials.username = email;
    if (const QString value = env("JIRA_API_TOKEN"); !value.isEmpty()) {
        credentials.token = value;
        credentials.rememberToken = false;   // it came from the environment; do not persist it
    }
    if (const QString value = env("JIRA_AUTH_MODE"); !value.isEmpty())
        credentials.mode = authModeFromString(value, credentials.mode);

    return credentials;
}

void Credentials::save() const
{
    QSettings settings;
    settings.setValue(QLatin1String(kBaseUrlKey), normalizeBaseUrl(baseUrl));
    settings.setValue(QLatin1String(kAuthModeKey), authModeToString(mode));
    settings.setValue(QLatin1String(kUsernameKey), username.trimmed());
    settings.setValue(QLatin1String(kRememberKey), rememberToken);
    settings.setValue(QLatin1String(kCaCertKey), caCertificatePath);
    settings.setValue(QLatin1String(kAllowInvalidKey), allowInvalidCertificates);

    // QSettings is a plain file. Writing the token there is a convenience the
    // user has to ask for, and the connection dialog says as much.
    if (rememberToken)
        settings.setValue(QLatin1String(kTokenKey), token);
    else
        settings.remove(QLatin1String(kTokenKey));
}

void Credentials::forgetToken()
{
    QSettings settings;
    settings.remove(QLatin1String(kTokenKey));
    settings.setValue(QLatin1String(kRememberKey), false);
}

} // namespace jira
