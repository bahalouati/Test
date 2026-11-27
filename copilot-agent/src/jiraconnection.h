#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <functional>

// Jira over its REST API v2, ported from qt-jira-client (JiraDesk) for Qt 5.12:
// the same credentials and the same settings keys, so a connection set up in
// JiraDesk is picked up here too.
namespace JiraConn {

enum class AuthMode {
    Basic,   // Jira Cloud: e-mail + API token
    Bearer   // Jira Server / Data Center: personal access token
};

QString normalizeBaseUrl(const QString &raw);
QUrl restEndpoint(const QString &baseUrl, const QString &resource, const QUrlQuery &query = QUrlQuery());

struct Credentials {
    QString baseUrl;
    AuthMode mode = AuthMode::Bearer;
    QString username;          // e-mail for Basic
    QString token;
    bool rememberToken = false;
    QString caCertificatePath;
    bool allowInvalidCertificates = false;

    bool isComplete() const;
    QByteArray authorizationHeader() const;

    // This app's settings; if it has none yet, JiraDesk's; then the
    // JIRA_BASE_URL / JIRA_USER (JIRA_EMAIL) / JIRA_API_TOKEN / JIRA_AUTH_MODE
    // environment variables override.
    static Credentials load();
    void save() const;
};

// "PROJ-123" (also from a pasted browse URL); empty when it is not a key.
QString issueKeyFrom(const QString &text);

struct Transition { QString id; QString name; QString toStatus; };

class Client : public QObject
{
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);

    void setCredentials(const Credentials &c);
    Credentials credentials() const { return m_creds; }
    bool isConfigured() const { return m_creds.isComplete(); }
    QUrl browseUrl(const QString &key) const;

    using JsonDone = std::function<void(const QJsonValue &body, const QString &error)>;

    void fetchMyself(JsonDone done);
    // Every field plus the field names, so text custom fields such as
    // "Acceptance Criteria" can be found by name on any instance.
    void fetchIssue(const QString &key, JsonDone done);
    void addComment(const QString &key, const QString &body, JsonDone done);
    void fetchTransitions(const QString &key, std::function<void(const QVector<Transition> &, const QString &error)> done);
    void applyTransition(const QString &key, const QString &transitionId, JsonDone done);
    // Attachment contents (the "content" URL of an attachment).
    void download(const QUrl &url, qint64 maxBytes, std::function<void(const QByteArray &, const QString &error)> done);

    static constexpr int kTimeoutMs = 30000;

private:
    QNetworkRequest request(const QUrl &url) const;
    void send(const QByteArray &verb, const QUrl &url, const QByteArray &body, JsonDone done);

    QNetworkAccessManager m_net;
    Credentials m_creds;
};

// The human message for a failed request: Jira's own errorMessages / errors
// when it sent some, else what the status code usually means.
QString errorFromResponse(int httpStatus, const QByteArray &body, const QString &transportError);

} // namespace JiraConn
