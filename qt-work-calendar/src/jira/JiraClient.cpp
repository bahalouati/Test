#include "jira/JiraClient.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

/*! How long a single request may take before it is given up on. */
const int kRequestTimeoutMs = 30000;

/*! Jira returns "2026-09-11T08:30:00.000+0200"; only the date part is needed. */
QDate dateOfWorklog(const QString &started)
{
    return QDate::fromString(started.left(10), Qt::ISODate);
}

QTime timeOfWorklog(const QString &started)
{
    if (started.size() < 16)
        return QTime();
    return QTime::fromString(started.mid(11, 5), QStringLiteral("HH:mm"));
}

/*! The display name of a nested object such as {"name": "..."} . */
QString nameOf(const QJsonObject &parent, const QString &key)
{
    const QJsonObject child = parent.value(key).toObject();
    if (child.contains(QStringLiteral("name")))
        return child.value(QStringLiteral("name")).toString();
    if (child.contains(QStringLiteral("value")))
        return child.value(QStringLiteral("value")).toString();
    return QString();
}

/*! Joins the "name" of every element of an array field. */
QString joinNames(const QJsonArray &array)
{
    QStringList names;
    for (const QJsonValue &value : array) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            names.append(name);
    }
    return names.join(QStringLiteral(", "));
}

} // namespace

bool JiraConfig::isUsable(QString *reason) const
{
    if (baseUrl.trimmed().isEmpty()) {
        if (reason)
            *reason = QStringLiteral("The Jira server address is missing.");
        return false;
    }
    if (!baseUrl.startsWith(QLatin1String("http"), Qt::CaseInsensitive)) {
        if (reason)
            *reason = QStringLiteral("The server address must start with http:// or https://");
        return false;
    }
    if (username.trimmed().isEmpty()) {
        if (reason)
            *reason = QStringLiteral("The Jira user name is missing.");
        return false;
    }
    if (token.isEmpty()) {
        if (reason)
            *reason = QStringLiteral("The Jira password or API token is missing.");
        return false;
    }
    return true;
}

JiraClient::JiraClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void JiraClient::setConfig(const JiraConfig &config)
{
    m_config = config;
    while (m_config.baseUrl.endsWith(QLatin1Char('/')))
        m_config.baseUrl.chop(1);
}

JiraConfig JiraClient::config() const
{
    return m_config;
}

void JiraClient::cancel()
{
    m_cancelRequested = true;
}

QByteArray JiraClient::get(const QString &path, const QString &query, QString *error)
{
    QUrl url(m_config.baseUrl + path);
    if (!query.isEmpty())
        url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");

    const QByteArray credentials =
        (m_config.username + QLatin1Char(':') + m_config.token).toUtf8().toBase64();
    request.setRawHeader("Authorization", "Basic " + credentials);

    QNetworkReply *reply = m_network->get(request);

    // Wait for this one request. The timer guarantees the loop always ends,
    // even if the server never answers.
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(kRequestTimeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        if (error)
            *error = QStringLiteral("%1 did not answer within %2 seconds.")
                         .arg(url.toString()).arg(kRequestTimeoutMs / 1000);
        return QByteArray();
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError) {
        if (error) {
            if (status == 401 || status == 403) {
                *error = QStringLiteral("Jira refused the credentials (HTTP %1). Check the user "
                                        "name and the API token.").arg(status);
            } else if (status > 0) {
                *error = QStringLiteral("Jira answered HTTP %1 for %2.\n%3")
                             .arg(status).arg(path, QString::fromUtf8(body.left(400)));
            } else {
                *error = QStringLiteral("Could not reach %1: %2")
                             .arg(m_config.baseUrl, networkErrorText);
            }
        }
        return QByteArray();
    }

    return body;
}

QString JiraClient::sprintNameOf(const QJsonObject &fields) const
{
    const QJsonValue sprintValue = fields.value(m_config.sprintField);
    if (sprintValue.isUndefined() || sprintValue.isNull())
        return QString();

    // Jira Server returns the sprint as a list of "com.atlassian...Sprint@1[id=...,name=X,...]"
    // strings; newer versions return objects. Both shapes are handled.
    QString lastEntry;
    if (sprintValue.isArray()) {
        const QJsonArray sprints = sprintValue.toArray();
        if (sprints.isEmpty())
            return QString();

        const QJsonValue last = sprints.last();
        if (last.isObject())
            return last.toObject().value(QStringLiteral("name")).toString();
        lastEntry = last.toString();
    } else if (sprintValue.isObject()) {
        return sprintValue.toObject().value(QStringLiteral("name")).toString();
    } else {
        lastEntry = sprintValue.toString();
    }

    const int nameIndex = lastEntry.indexOf(QStringLiteral("name="));
    if (nameIndex < 0)
        return lastEntry;

    QString name = lastEntry.mid(nameIndex + 5);
    const int comma = name.indexOf(QLatin1Char(','));
    if (comma >= 0)
        name = name.left(comma);
    return name.trimmed();
}

void JiraClient::findTestsheet(const QJsonObject &fields, QString *name, QString *url) const
{
    const QJsonArray attachments = fields.value(QStringLiteral("attachment")).toArray();
    const QString keyword = m_config.testsheetKeyword.toLower();
    if (keyword.isEmpty())
        return;

    for (const QJsonValue &value : attachments) {
        const QJsonObject attachment = value.toObject();
        const QString fileName = attachment.value(QStringLiteral("filename")).toString();
        if (!fileName.toLower().contains(keyword))
            continue;

        if (name)
            *name = fileName;
        if (url)
            *url = attachment.value(QStringLiteral("content")).toString();
        return;
    }
}

QString JiraClient::findMergeRequest(const QString &issueKey)
{
    QString ignoredError;
    const QByteArray body = get(QStringLiteral("/rest/api/2/issue/%1/remotelink").arg(issueKey),
                                QString(), &ignoredError);
    if (body.isEmpty())
        return QString(); // remote links are optional; a failure here is not fatal

    const QJsonArray links = QJsonDocument::fromJson(body).array();
    const QString marker = m_config.mergeRequestMarker;

    for (const QJsonValue &value : links) {
        const QString url = value.toObject()
                                .value(QStringLiteral("object")).toObject()
                                .value(QStringLiteral("url")).toString();
        if (!marker.isEmpty() && url.contains(marker))
            return url;
    }
    return QString();
}

WorkEntry JiraClient::entryFromWorklog(const QJsonObject &issue,
                                       const QJsonObject &worklog,
                                       const QString &mergeRequestUrl) const
{
    const QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
    const QString issueKey = issue.value(QStringLiteral("key")).toString();
    const QString started = worklog.value(QStringLiteral("started")).toString();

    WorkEntry entry;
    entry.source = EntrySource::Jira;
    entry.externalId = QStringLiteral("jira:%1:%2")
                           .arg(issueKey, worklog.value(QStringLiteral("id")).toString());

    entry.date = dateOfWorklog(started);
    entry.startTime = timeOfWorklog(started);
    entry.minutes = worklog.value(QStringLiteral("timeSpentSeconds")).toInt() / 60;
    if (entry.startTime.isValid() && entry.minutes > 0)
        entry.endTime = entry.startTime.addSecs(entry.minutes * 60);

    entry.issueKey = issueKey;
    entry.summary = fields.value(QStringLiteral("summary")).toString();
    entry.description = worklog.value(QStringLiteral("comment")).toString().trimmed();
    entry.issueType = nameOf(fields, QStringLiteral("issuetype"));
    entry.priority = priorityFromString(nameOf(fields, QStringLiteral("priority")));
    entry.component = joinNames(fields.value(QStringLiteral("components")).toArray());
    entry.fixVersion = joinNames(fields.value(QStringLiteral("fixVersions")).toArray());
    entry.sprint = sprintNameOf(fields);
    entry.mergeRequest = mergeRequestUrl;
    entry.issueUrl = QStringLiteral("%1/browse/%2").arg(m_config.baseUrl, issueKey);

    const QString statusName = nameOf(fields, QStringLiteral("status"));
    entry.status = entryStatusFromString(statusName);
    if (!statusName.isEmpty() && entry.status == EntryStatus::NotStarted) {
        // Jira status names vary by workflow; keep the original text visible
        // rather than pretending it maps onto one of ours.
        entry.tags = statusName;
    }

    findTestsheet(fields, &entry.testsheetName, &entry.testsheetUrl);

    // Jira does not say what kind of work a worklog was; development is the
    // honest default and the user can refine it afterwards.
    entry.activity = Activity::Development;
    entry.billable = true;

    if (entry.summary.isEmpty())
        entry.summary = issueKey;

    return entry;
}

bool JiraClient::fetchWorklogs(const QDate &from, const QDate &to,
                               JiraImportResult *result, QString *error)
{
    if (!result) {
        if (error)
            *error = QStringLiteral("Internal error: no destination for the import.");
        return false;
    }

    QString reason;
    if (!m_config.isUsable(&reason)) {
        if (error)
            *error = reason;
        return false;
    }
    if (!from.isValid() || !to.isValid() || from > to) {
        if (error)
            *error = QStringLiteral("The period is not valid.");
        return false;
    }

    m_cancelRequested = false;

    // Ask only for the issues this user booked time on in the period; the
    // worklogs themselves are fetched per issue, as the REST API requires.
    const QString jql = QStringLiteral(
                            "worklogAuthor = \"%1\" AND worklogDate >= \"%2\" "
                            "AND worklogDate <= \"%3\" ORDER BY key")
                            .arg(m_config.username,
                                 from.toString(Qt::ISODate),
                                 to.toString(Qt::ISODate));

    QUrlQuery searchQuery;
    searchQuery.addQueryItem(QStringLiteral("jql"), jql);
    searchQuery.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("500"));
    searchQuery.addQueryItem(QStringLiteral("fields"), QStringLiteral("*all"));

    emit progressChanged(0, 0, QStringLiteral("Searching for issues..."));

    const QByteArray searchBody = get(QStringLiteral("/rest/api/2/search"),
                                      searchQuery.toString(QUrl::FullyEncoded), error);
    if (searchBody.isEmpty())
        return false;

    const QJsonArray issues = QJsonDocument::fromJson(searchBody)
                                  .object()
                                  .value(QStringLiteral("issues"))
                                  .toArray();

    result->issuesScanned = issues.size();
    result->messages.append(QStringLiteral("%1 issues to inspect.").arg(issues.size()));

    int processed = 0;
    for (const QJsonValue &issueValue : issues) {
        if (m_cancelRequested) {
            result->cancelled = true;
            result->messages.append(QStringLiteral("Cancelled after %1 issues.").arg(processed));
            break;
        }

        const QJsonObject issue = issueValue.toObject();
        const QString issueKey = issue.value(QStringLiteral("key")).toString();

        ++processed;
        emit progressChanged(processed, issues.size(),
                             QStringLiteral("Reading %1").arg(issueKey));

        const QString mergeRequestUrl = findMergeRequest(issueKey);

        QString worklogError;
        const QByteArray worklogBody =
            get(QStringLiteral("/rest/api/2/issue/%1/worklog").arg(issueKey),
                QString(), &worklogError);
        if (worklogBody.isEmpty()) {
            result->messages.append(
                QStringLiteral("%1: could not read the worklogs (%2)").arg(issueKey, worklogError));
            continue;
        }

        const QJsonArray worklogs = QJsonDocument::fromJson(worklogBody)
                                        .object()
                                        .value(QStringLiteral("worklogs"))
                                        .toArray();

        int keptForIssue = 0;
        for (const QJsonValue &worklogValue : worklogs) {
            const QJsonObject worklog = worklogValue.toObject();
            const QJsonObject author = worklog.value(QStringLiteral("author")).toObject();

            // Shared issues carry everyone's time; keep only this user's.
            const QString authorName = author.value(QStringLiteral("name")).toString();
            const QString authorEmail = author.value(QStringLiteral("emailAddress")).toString();
            const bool isOurs =
                authorName.compare(m_config.username, Qt::CaseInsensitive) == 0
                || authorEmail.compare(m_config.username, Qt::CaseInsensitive) == 0;
            if (!isOurs)
                continue;

            const QDate worklogDate = dateOfWorklog(worklog.value(QStringLiteral("started")).toString());
            if (!worklogDate.isValid() || worklogDate < from || worklogDate > to)
                continue;

            WorkEntry entry = entryFromWorklog(issue, worklog, mergeRequestUrl);
            if (entry.minutes <= 0)
                continue;

            result->entries.append(entry);
            ++result->worklogsMatched;
            ++keptForIssue;
        }

        if (keptForIssue > 0) {
            result->messages.append(QStringLiteral("%1: %2 worklog(s) imported.")
                                        .arg(issueKey).arg(keptForIssue));
        }
    }

    emit progressChanged(processed, issues.size(), QStringLiteral("Finished."));
    return true;
}
