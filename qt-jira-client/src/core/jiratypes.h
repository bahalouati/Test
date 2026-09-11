#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// Plain data mirroring the subset of the Jira REST API v2 payloads this client
// reads. Parsing lives here (and only here) so it can be tested without a
// network, a window or an event loop.
namespace jira {

// Jira sends "2026-09-07T10:00:00.000+0000" -- an ISO 8601 timestamp with an
// offset that has no colon, which QDateTime's ISO parser rejects. Returns an
// invalid QDateTime for anything unparseable; callers render those as "-".
QDateTime parseDateTime(const QString &text);

// The inverse, for timestamps we send back (worklog "started").
QString formatDateTime(const QDateTime &dateTime);

// Seconds -> "3h 30m", the way Jira writes durations.
QString formatDuration(int seconds);

// Does the text match Jira's duration grammar ("2h", "1d 4h", "90m")? Checked
// before sending a worklog so a typo does not cost a round trip and a 400.
bool isValidDuration(const QString &text);

// "OPS-9" -> "OPS-000000009", so an issue key sorts by its number rather than
// lexically (which would put OPS-10 before OPS-9).
QString issueSortKey(const QString &key);

// Jira Server's sprint custom field is an array whose entries are the Java
// toString() of the sprint object:
//   com.atlassian.greenhopper.service.sprint.Sprint@1f[id=42,...,name=Sprint 12,...]
// Newer instances return a plain JSON object instead. Handles both shapes and
// returns the last entry's name -- the current sprint.
QString sprintNameFromField(const QJsonValue &field);

struct User {
    QString accountId;      // Cloud
    QString name;           // Server / Data Center username
    QString displayName;
    QString emailAddress;
    bool active = true;

    bool isNull() const;
    // Something to show in a table cell.
    QString label() const;

    static User fromJson(const QJsonObject &object);
};

struct Attachment {
    QString id;
    QString filename;
    QString contentUrl;   // "content" -- a direct download URL
    QString mimeType;
    qint64 size = 0;

    bool isNull() const { return filename.isEmpty(); }

    static QList<Attachment> listFromJson(const QJsonValue &value);
};

struct RemoteLink {
    QString title;
    QString url;

    static QList<RemoteLink> listFromJson(const QJsonValue &value);
};

// First attachment whose filename contains `marker`, case-insensitively.
Attachment findAttachment(const QList<Attachment> &attachments, const QString &marker);

// First remote link whose URL contains `marker` (e.g. "/merge_requests/").
QString findLinkUrl(const QList<RemoteLink> &links, const QString &marker);

struct Comment {
    QString id;
    User author;
    QString body;
    QDateTime created;
    QDateTime updated;

    static Comment fromJson(const QJsonObject &object);
    // Accepts both the /comment page object and a raw array holder.
    static QList<Comment> listFromJson(const QJsonObject &page);
};

struct Worklog {
    QString id;
    User author;
    QString comment;
    QDateTime started;
    int timeSpentSeconds = 0;
    QString timeSpent;

    static Worklog fromJson(const QJsonObject &object);
    static QList<Worklog> listFromJson(const QJsonObject &page);
};

struct Transition {
    QString id;
    QString name;
    QString toStatus;

    static QList<Transition> listFromJson(const QJsonObject &page);
};

struct Issue {
    QString id;
    QString key;
    QString summary;
    QString description;
    QString status;
    QString statusCategory;   // "new" / "indeterminate" / "done"
    QString issueType;
    QString priority;
    QString resolution;
    QString projectKey;
    QString projectName;
    User assignee;
    User reporter;
    QStringList labels;
    QStringList components;
    QStringList fixVersions;
    QString sprint;                 // resolved from a configurable custom field
    QList<Attachment> attachments;  // only populated when the field was requested
    QDateTime created;
    QDateTime updated;
    QDate dueDate;
    int timeSpentSeconds = 0;
    int originalEstimateSeconds = 0;

    bool isNull() const { return key.isEmpty(); }
    bool isResolved() const { return statusCategory == QLatin1String("done"); }

    static Issue fromJson(const QJsonObject &object);
};

struct SearchResult {
    int startAt = 0;
    int maxResults = 0;
    int total = 0;
    QList<Issue> issues;

    static SearchResult fromJson(const QJsonObject &object);
};

struct Project {
    QString id;
    QString key;
    QString name;

    static QList<Project> listFromJson(const QJsonValue &value);
};

// A failed request, boiled down to something worth putting in front of a user.
struct Error {
    int httpStatus = 0;
    QString message;
    QStringList details;

    bool isNull() const { return message.isEmpty() && httpStatus == 0; }
    bool isAuthenticationFailure() const { return httpStatus == 401 || httpStatus == 403; }
    QString toString() const;

    // Pulls "errorMessages" / "errors" out of a Jira error body when there is
    // one, and falls back to the transport error otherwise.
    static Error fromResponse(int httpStatus, const QByteArray &body, const QString &transportError);
};

} // namespace jira
