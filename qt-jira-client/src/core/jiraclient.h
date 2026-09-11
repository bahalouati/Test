#pragma once

#include "credentials.h"
#include "jiratypes.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace jira {

// Builds "<base>/rest/api/2/<resource>", keeping any context path the base URL
// carries. Free-standing so URL construction is testable on its own.
QUrl restEndpoint(const QString &baseUrl, const QString &resource, const QUrlQuery &query = QUrlQuery());

// The result of one in-flight request. Each Client call hands back a Reply that
// emits exactly one of succeeded/failed and then deletes itself, which keeps
// call sites to a lambda instead of a web of per-endpoint signals.
class Reply : public QObject
{
    Q_OBJECT

public:
    explicit Reply(QObject *parent = nullptr);

    // Convenience for callers that only care about an object body.
    QJsonObject object() const { return m_body.toObject(); }

signals:
    void succeeded(const QJsonValue &body);
    void failed(const jira::Error &error);

private:
    friend class Client;
    void finishWith(const QJsonValue &body);
    void failWith(const Error &error);

    QJsonValue m_body;
};

// A thin asynchronous wrapper over the Jira REST API v2. Version 2 is the one
// that works on Jira Server / Data Center, and its plain-text comment and
// description bodies are far easier to render than Cloud v3's ADF documents.
class Client : public QObject
{
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);

    Credentials credentials() const { return m_credentials; }
    void setCredentials(const Credentials &credentials);

    bool isConfigured() const { return m_credentials.isComplete(); }
    int pendingRequests() const { return m_pending; }

    // The browser URL for an issue, for "Open in Jira".
    QUrl browseUrl(const QString &issueKey) const;

    // Endpoints. Every one returns a Reply owned by this Client.
    Reply *fetchMyself();
    Reply *search(const QString &jql, int startAt, int maxResults);
    Reply *fetchIssue(const QString &issueKey);
    Reply *fetchComments(const QString &issueKey);
    Reply *addComment(const QString &issueKey, const QString &body);
    Reply *fetchWorklogs(const QString &issueKey);
    Reply *addWorklog(const QString &issueKey,
                      const QString &timeSpent,
                      const QDateTime &started,
                      const QString &comment);
    Reply *fetchTransitions(const QString &issueKey);
    Reply *applyTransition(const QString &issueKey, const QString &transitionId);
    Reply *fetchProjects();

    // The field set search asks for. Requesting these by name rather than
    // taking Jira's default keeps result pages small on big instances.
    static QString searchFields();

signals:
    void busyChanged(bool busy);

private:
    Reply *send(const QByteArray &verb, const QUrl &url, const QJsonObject &body, bool hasBody);
    void applyTlsConfiguration(QNetworkRequest &request) const;
    void handleReply(QNetworkReply *networkReply, Reply *reply);

    QNetworkAccessManager *m_network;
    Credentials m_credentials;
    int m_pending = 0;
};

} // namespace jira

Q_DECLARE_METATYPE(jira::Error)
