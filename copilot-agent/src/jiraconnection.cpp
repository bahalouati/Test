#include "jiraconnection.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>

namespace JiraConn {

namespace {
// The same keys as JiraDesk.
const char *kBaseUrlKey = "connection/baseUrl";
const char *kAuthModeKey = "connection/authMode";
const char *kUsernameKey = "connection/username";
const char *kTokenKey = "connection/token";
const char *kRememberKey = "connection/rememberToken";
const char *kCaCertKey = "connection/caCertificate";
const char *kAllowInvalidKey = "connection/allowInvalidCertificates";

AuthMode modeFrom(const QString &text, AuthMode fallback)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("bearer"))
        return AuthMode::Bearer;
    if (t == QLatin1String("basic"))
        return AuthMode::Basic;
    return fallback;
}

Credentials readFrom(QSettings &s)
{
    Credentials c;
    c.baseUrl = s.value(QLatin1String(kBaseUrlKey)).toString();
    c.mode = modeFrom(s.value(QLatin1String(kAuthModeKey)).toString(), c.mode);
    c.username = s.value(QLatin1String(kUsernameKey)).toString();
    c.rememberToken = s.value(QLatin1String(kRememberKey), false).toBool();
    c.caCertificatePath = s.value(QLatin1String(kCaCertKey)).toString();
    c.allowInvalidCertificates = s.value(QLatin1String(kAllowInvalidKey), false).toBool();
    if (c.rememberToken)
        c.token = s.value(QLatin1String(kTokenKey)).toString();
    return c;
}
} // namespace

QString normalizeBaseUrl(const QString &raw)
{
    QString url = raw.trimmed();
    if (url.isEmpty())
        return url;
    if (!url.contains(QLatin1String("://")))
        url.prepend(QLatin1String("https://"));
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    static const QStringList suffixes = {
        QStringLiteral("/rest/api/2"), QStringLiteral("/rest/api/3"), QStringLiteral("/rest/api/latest"),
        QStringLiteral("/secure/Dashboard.jspa"),
    };
    for (const QString &s : suffixes) {
        if (url.endsWith(s, Qt::CaseInsensitive)) {
            url.chop(s.size());
            break;
        }
    }
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

QUrl restEndpoint(const QString &baseUrl, const QString &resource, const QUrlQuery &query)
{
    const QString root = normalizeBaseUrl(baseUrl);
    if (root.isEmpty())
        return QUrl();
    QString path = resource;
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    QUrl url(root + QLatin1String("/rest/api/2/") + path);
    if (!query.isEmpty())
        url.setQuery(query);
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
        return QByteArray();
    if (mode == AuthMode::Bearer)
        return "Bearer " + token.trimmed().toUtf8();
    return "Basic " + (username.trimmed().toUtf8() + ':' + token.trimmed().toUtf8()).toBase64();
}

Credentials Credentials::load()
{
    QSettings ours;
    Credentials c = readFrom(ours);
    if (c.baseUrl.isEmpty()) {
        // Set up already in JiraDesk? Use that connection.
        QSettings jiraDesk(QStringLiteral("JiraDesk"), QStringLiteral("JiraDesk"));
        c = readFrom(jiraDesk);
    }
    auto env = [](const char *name) { return QString::fromLocal8Bit(qgetenv(name)).trimmed(); };
    QString v;
    if (!(v = env("JIRA_BASE_URL")).isEmpty())
        c.baseUrl = v;
    if (!(v = env("JIRA_USER")).isEmpty())
        c.username = v;
    else if (!(v = env("JIRA_EMAIL")).isEmpty())
        c.username = v;
    if (!(v = env("JIRA_API_TOKEN")).isEmpty()) {
        c.token = v;
        c.rememberToken = false;   // from the environment: never written to disk
    }
    if (!(v = env("JIRA_AUTH_MODE")).isEmpty())
        c.mode = modeFrom(v, c.mode);
    return c;
}

void Credentials::save() const
{
    QSettings s;
    s.setValue(QLatin1String(kBaseUrlKey), normalizeBaseUrl(baseUrl));
    s.setValue(QLatin1String(kAuthModeKey), mode == AuthMode::Bearer ? QStringLiteral("bearer") : QStringLiteral("basic"));
    s.setValue(QLatin1String(kUsernameKey), username.trimmed());
    s.setValue(QLatin1String(kRememberKey), rememberToken);
    s.setValue(QLatin1String(kCaCertKey), caCertificatePath);
    s.setValue(QLatin1String(kAllowInvalidKey), allowInvalidCertificates);
    // The settings file is plain text: the token goes there only when asked.
    if (rememberToken)
        s.setValue(QLatin1String(kTokenKey), token);
    else
        s.remove(QLatin1String(kTokenKey));
}

QString issueKeyFrom(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("(?:^|[^A-Za-z0-9])([A-Za-z][A-Za-z0-9_]{0,19}-[0-9]{1,9})(?![0-9])"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    return m.hasMatch() ? m.captured(1).toUpper() : QString();
}

QString errorFromResponse(int httpStatus, const QByteArray &body, const QString &transportError)
{
    QStringList details;
    const QJsonObject payload = QJsonDocument::fromJson(body).object();
    for (const QJsonValue &e : payload.value(QLatin1String("errorMessages")).toArray())
        if (!e.toString().isEmpty())
            details << e.toString();
    const QJsonObject fieldErrors = payload.value(QLatin1String("errors")).toObject();
    for (auto it = fieldErrors.begin(); it != fieldErrors.end(); ++it)
        details << QStringLiteral("%1: %2").arg(it.key(), it.value().toString());

    QString message;
    switch (httpStatus) {
    case 401:
        message = QObject::tr("Jira rejected the credentials (401). Check the user name and token, and that the "
                              "authentication type matches your Jira (Cloud: e-mail + API token; Server/Data Center: "
                              "personal access token).");
        break;
    case 403:
        message = QObject::tr("Jira refused the request (403). The token may lack permission, or the account may need "
                              "to sign in once in the browser.");
        break;
    case 404:
        message = QObject::tr("Not found (404). Check the issue key and the server URL; a Jira under a context path "
                              "needs it included, e.g. https://jira.example.com/jira.");
        break;
    case 429:
        message = QObject::tr("Rate limited by Jira (429). Wait a moment and try again.");
        break;
    default:
        if (httpStatus >= 500)
            message = QObject::tr("Jira had an internal error (%1). Try again later.").arg(httpStatus);
        else if (httpStatus >= 400)
            message = QObject::tr("Jira answered with an error (%1).").arg(httpStatus);
        else
            message = transportError.isEmpty() ? QObject::tr("Could not reach Jira.") : transportError;
    }
    if (!details.isEmpty())
        message += QLatin1Char(' ') + details.join(QStringLiteral("; "));
    return message;
}

Client::Client(QObject *parent)
    : QObject(parent)
{
}

void Client::setCredentials(const Credentials &c)
{
    m_creds = c;
    m_creds.baseUrl = normalizeBaseUrl(c.baseUrl);
}

QUrl Client::browseUrl(const QString &key) const
{
    const QString root = normalizeBaseUrl(m_creds.baseUrl);
    return root.isEmpty() || key.isEmpty() ? QUrl() : QUrl(root + QLatin1String("/browse/") + key);
}

QNetworkRequest Client::request(const QUrl &url) const
{
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "CopilotAgent (Qt)");
    // Without this, Jira Server answers an unauthenticated call with an
    // HTML login page instead of a 401.
    req.setRawHeader("X-Atlassian-Token", "no-check");
    const QByteArray auth = m_creds.authorizationHeader();
    if (!auth.isEmpty())
        req.setRawHeader("Authorization", auth);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    // Self-hosted Jira often uses an internal certificate authority.
    if (!m_creds.caCertificatePath.isEmpty()) {
        QFile f(m_creds.caCertificatePath);
        if (f.open(QIODevice::ReadOnly)) {
            const QList<QSslCertificate> extra = QSslCertificate::fromDevice(&f, QSsl::Pem);
            if (!extra.isEmpty()) {
                QSslConfiguration conf = req.sslConfiguration();
                conf.setCaCertificates(conf.caCertificates() + extra);
                req.setSslConfiguration(conf);
            }
        }
    }
    return req;
}

void Client::send(const QByteArray &verb, const QUrl &url, const QByteArray &body, JsonDone done)
{
    if (!url.isValid() || url.host().isEmpty()) {
        QTimer::singleShot(0, this, [done] {
            done(QJsonValue(), QObject::tr("No Jira server is set up. Open \"Jira settings\" and enter the server URL and token."));
        });
        return;
    }
    QNetworkRequest req = request(url);
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_net.sendCustomRequest(req, verb, body);
    if (m_creds.allowInvalidCertificates)
        connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->ignoreSslErrors(); });
    // Qt 5.12 has no per-request timeout.
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(kTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            QString transport;
            if (reply->error() == QNetworkReply::OperationCanceledError)
                transport = QObject::tr("Jira did not answer within %1 s.").arg(kTimeoutMs / 1000);
            else if (reply->error() != QNetworkReply::NoError && status < 400)
                transport = reply->errorString();
            done(QJsonValue(), errorFromResponse(status, payload, transport));
            return;
        }
        if (payload.trimmed().isEmpty()) {           // 204, e.g. after a transition
            done(QJsonObject(), QString());
            return;
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &pe);
        if (pe.error != QJsonParseError::NoError) {
            done(QJsonValue(), QObject::tr("Jira did not return JSON: the URL may point at something other than Jira, "
                                           "or a sign-in page came back."));
            return;
        }
        done(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()), QString());
    });
}

void Client::fetchMyself(JsonDone done)
{
    send("GET", restEndpoint(m_creds.baseUrl, QStringLiteral("myself")), QByteArray(), done);
}

void Client::fetchIssue(const QString &key, JsonDone done)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("fields"), QStringLiteral("*all"));
    q.addQueryItem(QStringLiteral("expand"), QStringLiteral("names"));
    send("GET", restEndpoint(m_creds.baseUrl, QStringLiteral("issue/") + key, q), QByteArray(), done);
}

void Client::addComment(const QString &key, const QString &body, JsonDone done)
{
    const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("body"), body}}).toJson(QJsonDocument::Compact);
    send("POST", restEndpoint(m_creds.baseUrl, QStringLiteral("issue/%1/comment").arg(key)), json, done);
}

void Client::fetchTransitions(const QString &key, std::function<void(const QVector<Transition> &, const QString &)> done)
{
    send("GET", restEndpoint(m_creds.baseUrl, QStringLiteral("issue/%1/transitions").arg(key)), QByteArray(),
         [done](const QJsonValue &body, const QString &error) {
        QVector<Transition> out;
        for (const QJsonValue &v : body.toObject().value(QStringLiteral("transitions")).toArray()) {
            const QJsonObject t = v.toObject();
            out.append({t.value(QStringLiteral("id")).toString(), t.value(QStringLiteral("name")).toString(),
                        t.value(QStringLiteral("to")).toObject().value(QStringLiteral("name")).toString()});
        }
        done(out, error);
    });
}

void Client::applyTransition(const QString &key, const QString &transitionId, JsonDone done)
{
    const QJsonObject body{{QStringLiteral("transition"), QJsonObject{{QStringLiteral("id"), transitionId}}}};
    send("POST", restEndpoint(m_creds.baseUrl, QStringLiteral("issue/%1/transitions").arg(key)),
         QJsonDocument(body).toJson(QJsonDocument::Compact), done);
}

void Client::download(const QUrl &url, qint64 maxBytes, std::function<void(const QByteArray &, const QString &)> done)
{
    QNetworkRequest req = request(url);
    req.setRawHeader("Accept", "*/*");
    // Attachments may live on another host (Cloud's media servers): follow redirects.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net.get(req);
    if (m_creds.allowInvalidCertificates)
        connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->ignoreSslErrors(); });
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(kTimeoutMs);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64) {
        if (received > maxBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, done, maxBytes] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            done(QByteArray(), reply->error() == QNetworkReply::OperationCanceledError
                                   ? QObject::tr("too large (over %1 MB) or too slow").arg(maxBytes / (1024 * 1024))
                                   : errorFromResponse(status, QByteArray(), reply->errorString()));
            return;
        }
        done(reply->readAll(), QString());
    });
}

} // namespace JiraConn
