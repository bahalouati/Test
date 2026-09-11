#include "jiraclient.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>

namespace jira {

namespace {
constexpr int kRequestTimeoutMs = 30000;
} // namespace

QUrl restEndpoint(const QString &baseUrl, const QString &resource, const QUrlQuery &query)
{
    const QString root = normalizeBaseUrl(baseUrl);
    if (root.isEmpty())
        return {};

    QString path = resource;
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);

    QUrl url(root + QLatin1String("/rest/api/2/") + path);
    if (!query.isEmpty())
        url.setQuery(query);
    return url;
}

Reply::Reply(QObject *parent)
    : QObject(parent)
{
}

void Reply::finishWith(const QJsonValue &body)
{
    m_body = body;
    emit succeeded(body);
    deleteLater();
}

void Reply::failWith(const Error &error)
{
    emit failed(error);
    deleteLater();
}

Client::Client(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_network->setAutoDeleteReplies(false);
}

void Client::setCredentials(const Credentials &credentials)
{
    m_credentials = credentials;
    m_credentials.baseUrl = normalizeBaseUrl(credentials.baseUrl);
}

QUrl Client::browseUrl(const QString &issueKey) const
{
    const QString root = normalizeBaseUrl(m_credentials.baseUrl);
    if (root.isEmpty() || issueKey.isEmpty())
        return {};
    return QUrl(root + QLatin1String("/browse/") + issueKey);
}

QString Client::searchFields()
{
    return QStringLiteral("summary,status,issuetype,priority,assignee,reporter,project,labels,"
                          "components,created,updated,resolution,duedate,timespent,timeoriginalestimate,description");
}

Reply *Client::send(const QByteArray &verb, const QUrl &url, const QJsonObject &body, bool hasBody)
{
    auto *reply = new Reply(this);

    if (!url.isValid() || url.host().isEmpty()) {
        Error error;
        error.message = tr("No Jira server is configured. Use Connect… to set the server URL and API token.");
        // Report asynchronously so callers can connect after this returns.
        QTimer::singleShot(0, reply, [reply, error] { reply->failWith(error); });
        return reply;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "JiraDesk (Qt)");
    // Jira Server answers an unauthenticated request with an HTML login page
    // instead of a 401 unless it is told not to negotiate a browser login.
    request.setRawHeader("X-Atlassian-Token", "no-check");
    const QByteArray authorization = m_credentials.authorizationHeader();
    if (!authorization.isEmpty())
        request.setRawHeader("Authorization", authorization);
    if (hasBody)
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(kRequestTimeoutMs);
    applyTlsConfiguration(request);

    const QByteArray payload = hasBody ? QJsonDocument(body).toJson(QJsonDocument::Compact) : QByteArray();
    QNetworkReply *networkReply = m_network->sendCustomRequest(request, verb, payload);

    if (m_credentials.allowInvalidCertificates) {
        connect(networkReply, &QNetworkReply::sslErrors, networkReply, [networkReply] {
            networkReply->ignoreSslErrors();
        });
    }

    if (m_pending++ == 0)
        emit busyChanged(true);

    connect(networkReply, &QNetworkReply::finished, this, [this, networkReply, reply] {
        handleReply(networkReply, reply);
    });
    return reply;
}

void Client::applyTlsConfiguration(QNetworkRequest &request) const
{
    if (m_credentials.caCertificatePath.isEmpty())
        return;

    QFile file(m_credentials.caCertificatePath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QList<QSslCertificate> extra = QSslCertificate::fromDevice(&file, QSsl::Pem);
    if (extra.isEmpty())
        return;

    QSslConfiguration configuration = request.sslConfiguration();
    configuration.setCaCertificates(configuration.caCertificates() + extra);
    request.setSslConfiguration(configuration);
}

void Client::handleReply(QNetworkReply *networkReply, Reply *reply)
{
    networkReply->deleteLater();
    if (--m_pending == 0)
        emit busyChanged(false);

    const int status = networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = networkReply->readAll();

    if (networkReply->error() != QNetworkReply::NoError || status >= 400) {
        QString transportError;
        if (networkReply->error() != QNetworkReply::NoError && status < 400)
            transportError = networkReply->errorString();
        reply->failWith(Error::fromResponse(status, payload, transportError));
        return;
    }

    // 204 No Content is the normal answer to a transition.
    if (payload.trimmed().isEmpty()) {
        reply->finishWith(QJsonObject());
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        Error error;
        error.httpStatus = status;
        error.message = tr("Jira did not return JSON. This usually means the URL points at something "
                           "other than a Jira REST API, or a sign-in page was returned instead.");
        error.details.append(parseError.errorString());
        reply->failWith(error);
        return;
    }

    reply->finishWith(document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()));
}

Reply *Client::fetchMyself()
{
    return send("GET", restEndpoint(m_credentials.baseUrl, QStringLiteral("myself")), {}, false);
}

Reply *Client::search(const QString &jql, int startAt, int maxResults)
{
    return search(jql, startAt, maxResults, QStringList());
}

Reply *Client::search(const QString &jql, int startAt, int maxResults, const QStringList &extraFields)
{
    // POST rather than GET: JQL routinely outgrows what a proxy will accept in
    // a query string, and this sidesteps every encoding question.
    QJsonObject body;
    body.insert(QStringLiteral("jql"), jql);
    body.insert(QStringLiteral("startAt"), startAt);
    body.insert(QStringLiteral("maxResults"), maxResults);

    QJsonArray fields;
    QStringList names = searchFields().split(QLatin1Char(','));
    for (const QString &extra : extraFields) {
        const QString trimmed = extra.trimmed();
        if (!trimmed.isEmpty() && !names.contains(trimmed))
            names.append(trimmed);
    }
    for (const QString &field : names)
        fields.append(field);
    body.insert(QStringLiteral("fields"), fields);

    return send("POST", restEndpoint(m_credentials.baseUrl, QStringLiteral("search")), body, true);
}

Reply *Client::fetchIssue(const QString &issueKey)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fields"), searchFields());
    return send("GET",
                restEndpoint(m_credentials.baseUrl, QStringLiteral("issue/") + issueKey, query),
                {},
                false);
}

Reply *Client::fetchComments(const QString &issueKey)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("100"));
    query.addQueryItem(QStringLiteral("orderBy"), QStringLiteral("-created"));
    return send("GET",
                restEndpoint(m_credentials.baseUrl, QStringLiteral("issue/%1/comment").arg(issueKey), query),
                {},
                false);
}

Reply *Client::addComment(const QString &issueKey, const QString &body)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("body"), body);
    return send("POST",
                restEndpoint(m_credentials.baseUrl, QStringLiteral("issue/%1/comment").arg(issueKey)),
                payload,
                true);
}

Reply *Client::fetchWorklogs(const QString &issueKey)
{
    return send("GET",
                restEndpoint(m_credentials.baseUrl, QStringLiteral("issue/%1/worklog").arg(issueKey)),
                {},
                false);
}

Reply *Client::addWorklog(const QString &issueKey,
                          const QString &timeSpent,
                          const QDateTime &started,
                          const QString &comment)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("timeSpent"), timeSpent.trimmed());
    if (started.isValid())
        payload.insert(QStringLiteral("started"), formatDateTime(started));
    if (!comment.trimmed().isEmpty())
        payload.insert(QStringLiteral("comment"), comment);
    return send("POST",
                restEndpoint(m_credentials.baseUrl, QStringLiteral("issue/%1/worklog").arg(issueKey)),
                payload,
                true);
}

Reply *Client::fetchTransitions(const QString &issueKey)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("expand"), QStringLiteral("transitions.fields"));
    return send("GET",
                restEndpoint(m_credentials.baseUrl, QStringLiteral("issue/%1/transitions").arg(issueKey), query),
                {},
                false);
}

Reply *Client::applyTransition(const QString &issueKey, const QString &transitionId)
{
    QJsonObject transition;
    transition.insert(QStringLiteral("id"), transitionId);
    QJsonObject payload;
    payload.insert(QStringLiteral("transition"), transition);
    return send("POST",
                restEndpoint(m_credentials.baseUrl, QStringLiteral("issue/%1/transitions").arg(issueKey)),
                payload,
                true);
}

Reply *Client::fetchRemoteLinks(const QString &issueKey)
{
    return send("GET",
                restEndpoint(m_credentials.baseUrl, QStringLiteral("issue/%1/remotelink").arg(issueKey)),
                {},
                false);
}

Reply *Client::fetchProjects()
{
    return send("GET", restEndpoint(m_credentials.baseUrl, QStringLiteral("project")), {}, false);
}

} // namespace jira
