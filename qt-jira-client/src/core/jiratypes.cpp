#include "jiratypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QRegularExpression>

namespace jira {

namespace {

QString stringAt(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString();
}

// Jira nulls out empty object fields (assignee, resolution, priority ...), so
// value(...).toObject() on a null is normal rather than exceptional.
QJsonObject objectAt(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toObject();
}

QStringList namesOf(const QJsonValue &value, const char *key)
{
    QStringList names;
    const QJsonArray array = value.toArray();
    names.reserve(array.size());
    for (const QJsonValue &entry : array)
        names.append(entry.isString() ? entry.toString() : entry.toObject().value(QLatin1String(key)).toString());
    names.removeAll(QString());
    return names;
}

} // namespace

QDateTime parseDateTime(const QString &text)
{
    if (text.isEmpty())
        return {};

    // "+0000" -> "+00:00" so the ISO parser accepts the offset.
    static const QRegularExpression offset(QStringLiteral("([+-])(\\d{2})(\\d{2})$"));
    QString normalised = text;
    const QRegularExpressionMatch match = offset.match(normalised);
    if (match.hasMatch()) {
        normalised.replace(match.capturedStart(),
                           match.capturedLength(),
                           match.captured(1) + match.captured(2) + QLatin1Char(':') + match.captured(3));
    }

    QDateTime parsed = QDateTime::fromString(normalised, Qt::ISODateWithMs);
    if (!parsed.isValid())
        parsed = QDateTime::fromString(normalised, Qt::ISODate);
    return parsed;
}

QString formatDateTime(const QDateTime &dateTime)
{
    if (!dateTime.isValid())
        return {};
    // Jira wants the offset without a colon, which is exactly what ISODateWithMs
    // does not give us -- so put it back the way it came.
    QString text = dateTime.toString(Qt::ISODateWithMs);
    static const QRegularExpression offset(QStringLiteral("([+-])(\\d{2}):(\\d{2})$"));
    const QRegularExpressionMatch match = offset.match(text);
    if (match.hasMatch()) {
        text.replace(match.capturedStart(),
                     match.capturedLength(),
                     match.captured(1) + match.captured(2) + match.captured(3));
    } else if (text.endsWith(QLatin1Char('Z'))) {
        text.chop(1);
        text.append(QStringLiteral("+0000"));
    }
    return text;
}

QString formatDuration(int seconds)
{
    if (seconds <= 0)
        return QStringLiteral("-");

    const int minutes = seconds / 60;
    const int hours = minutes / 60;
    const int remainingMinutes = minutes % 60;
    if (hours > 0 && remainingMinutes > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(remainingMinutes);
    if (hours > 0)
        return QStringLiteral("%1h").arg(hours);
    return QStringLiteral("%1m").arg(qMax(1, remainingMinutes));
}

bool isValidDuration(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(\\d+(?:\\.\\d+)?[wdhm]\\s*)+$"),
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern.match(text).hasMatch();
}

QString sprintNameFromField(const QJsonValue &field)
{
    const QJsonArray entries = field.toArray();
    // The last entry is the current sprint; earlier ones are sprints the issue
    // has already been through.
    for (int index = entries.size() - 1; index >= 0; --index) {
        const QJsonValue entry = entries.at(index);

        // Modern shape: a real object.
        if (entry.isObject()) {
            const QString name = entry.toObject().value(QLatin1String("name")).toString();
            if (!name.isEmpty())
                return name;
            continue;
        }

        // Jira Server shape: the Java toString(), with name= in a bracketed list.
        const QString text = entry.toString();
        const qsizetype start = text.indexOf(QLatin1String("name="));
        if (start < 0)
            continue;
        const qsizetype from = start + 5;
        qsizetype end = text.indexOf(QLatin1Char(','), from);
        if (end < 0)
            end = text.indexOf(QLatin1Char(']'), from);
        if (end < 0)
            end = text.size();
        const QString name = text.mid(from, end - from).trimmed();
        if (!name.isEmpty())
            return name;
    }

    // A single object rather than an array happens on some configurations.
    if (field.isObject())
        return field.toObject().value(QLatin1String("name")).toString();
    return {};
}

QList<Attachment> Attachment::listFromJson(const QJsonValue &value)
{
    QList<Attachment> attachments;
    const QJsonArray array = value.toArray();
    attachments.reserve(array.size());
    for (const QJsonValue &entry : array) {
        const QJsonObject object = entry.toObject();
        Attachment attachment;
        attachment.id = stringAt(object, "id");
        attachment.filename = stringAt(object, "filename");
        attachment.contentUrl = stringAt(object, "content");
        attachment.mimeType = stringAt(object, "mimeType");
        attachment.size = qint64(object.value(QLatin1String("size")).toDouble());
        if (!attachment.filename.isEmpty())
            attachments.append(attachment);
    }
    return attachments;
}

QList<RemoteLink> RemoteLink::listFromJson(const QJsonValue &value)
{
    QList<RemoteLink> links;
    const QJsonArray array = value.toArray();
    links.reserve(array.size());
    for (const QJsonValue &entry : array) {
        const QJsonObject object = entry.toObject().value(QLatin1String("object")).toObject();
        RemoteLink link;
        link.url = object.value(QLatin1String("url")).toString();
        link.title = object.value(QLatin1String("title")).toString();
        if (!link.url.isEmpty())
            links.append(link);
    }
    return links;
}

Attachment findAttachment(const QList<Attachment> &attachments, const QString &marker)
{
    if (marker.isEmpty())
        return {};
    for (const Attachment &attachment : attachments) {
        if (attachment.filename.contains(marker, Qt::CaseInsensitive))
            return attachment;
    }
    return {};
}

QString findLinkUrl(const QList<RemoteLink> &links, const QString &marker)
{
    if (marker.isEmpty())
        return {};
    for (const RemoteLink &link : links) {
        if (link.url.contains(marker, Qt::CaseInsensitive))
            return link.url;
    }
    return {};
}

QString issueSortKey(const QString &key)
{
    const qsizetype dash = key.lastIndexOf(QLatin1Char('-'));
    if (dash <= 0)
        return key;
    bool ok = false;
    const int number = QStringView(key).mid(dash + 1).toInt(&ok);
    if (!ok)
        return key;
    return key.left(dash) + QStringLiteral("-%1").arg(number, 9, 10, QLatin1Char('0'));
}

bool User::isNull() const
{
    return accountId.isEmpty() && name.isEmpty() && displayName.isEmpty();
}

QString User::label() const
{
    if (!displayName.isEmpty())
        return displayName;
    if (!name.isEmpty())
        return name;
    if (!emailAddress.isEmpty())
        return emailAddress;
    return QObject::tr("Unassigned");
}

User User::fromJson(const QJsonObject &object)
{
    User user;
    user.accountId = stringAt(object, "accountId");
    user.name = stringAt(object, "name");
    user.displayName = stringAt(object, "displayName");
    user.emailAddress = stringAt(object, "emailAddress");
    user.active = object.value(QLatin1String("active")).toBool(true);
    return user;
}

Comment Comment::fromJson(const QJsonObject &object)
{
    Comment comment;
    comment.id = stringAt(object, "id");
    comment.author = User::fromJson(objectAt(object, "author"));
    // v2 comment bodies are plain text; v3 would hand us an ADF document here,
    // which is a large part of why this client speaks v2.
    comment.body = stringAt(object, "body");
    comment.created = parseDateTime(stringAt(object, "created"));
    comment.updated = parseDateTime(stringAt(object, "updated"));
    return comment;
}

QList<Comment> Comment::listFromJson(const QJsonObject &page)
{
    QList<Comment> comments;
    const QJsonArray array = page.value(QLatin1String("comments")).toArray();
    comments.reserve(array.size());
    for (const QJsonValue &entry : array)
        comments.append(Comment::fromJson(entry.toObject()));
    return comments;
}

Worklog Worklog::fromJson(const QJsonObject &object)
{
    Worklog worklog;
    worklog.id = stringAt(object, "id");
    worklog.author = User::fromJson(objectAt(object, "author"));
    worklog.comment = stringAt(object, "comment");
    worklog.started = parseDateTime(stringAt(object, "started"));
    worklog.timeSpentSeconds = object.value(QLatin1String("timeSpentSeconds")).toInt();
    worklog.timeSpent = stringAt(object, "timeSpent");
    if (worklog.timeSpent.isEmpty())
        worklog.timeSpent = formatDuration(worklog.timeSpentSeconds);
    return worklog;
}

QList<Worklog> Worklog::listFromJson(const QJsonObject &page)
{
    QList<Worklog> worklogs;
    const QJsonArray array = page.value(QLatin1String("worklogs")).toArray();
    worklogs.reserve(array.size());
    for (const QJsonValue &entry : array)
        worklogs.append(Worklog::fromJson(entry.toObject()));
    return worklogs;
}

QList<Transition> Transition::listFromJson(const QJsonObject &page)
{
    QList<Transition> transitions;
    const QJsonArray array = page.value(QLatin1String("transitions")).toArray();
    transitions.reserve(array.size());
    for (const QJsonValue &entry : array) {
        const QJsonObject object = entry.toObject();
        Transition transition;
        transition.id = stringAt(object, "id");
        transition.name = stringAt(object, "name");
        transition.toStatus = stringAt(objectAt(object, "to"), "name");
        if (!transition.id.isEmpty())
            transitions.append(transition);
    }
    return transitions;
}

Issue Issue::fromJson(const QJsonObject &object)
{
    Issue issue;
    issue.id = stringAt(object, "id");
    issue.key = stringAt(object, "key");

    const QJsonObject fields = objectAt(object, "fields");
    issue.summary = stringAt(fields, "summary");
    issue.description = stringAt(fields, "description");

    const QJsonObject status = objectAt(fields, "status");
    issue.status = stringAt(status, "name");
    issue.statusCategory = stringAt(objectAt(status, "statusCategory"), "key");

    issue.issueType = stringAt(objectAt(fields, "issuetype"), "name");
    issue.priority = stringAt(objectAt(fields, "priority"), "name");
    issue.resolution = stringAt(objectAt(fields, "resolution"), "name");

    const QJsonObject project = objectAt(fields, "project");
    issue.projectKey = stringAt(project, "key");
    issue.projectName = stringAt(project, "name");

    issue.assignee = User::fromJson(objectAt(fields, "assignee"));
    issue.reporter = User::fromJson(objectAt(fields, "reporter"));

    issue.labels = namesOf(fields.value(QLatin1String("labels")), "name");
    issue.components = namesOf(fields.value(QLatin1String("components")), "name");
    issue.fixVersions = namesOf(fields.value(QLatin1String("fixVersions")), "name");
    issue.attachments = Attachment::listFromJson(fields.value(QLatin1String("attachment")));

    // The sprint lives in a custom field whose id differs per instance, so scan
    // for any customfield_* that parses as a sprint rather than hard-coding one.
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (!it.key().startsWith(QLatin1String("customfield_")) || it.value().isNull())
            continue;
        const QString name = sprintNameFromField(it.value());
        if (!name.isEmpty()) {
            issue.sprint = name;
            break;
        }
    }

    issue.created = parseDateTime(stringAt(fields, "created"));
    issue.updated = parseDateTime(stringAt(fields, "updated"));
    issue.dueDate = QDate::fromString(stringAt(fields, "duedate"), Qt::ISODate);

    issue.timeSpentSeconds = fields.value(QLatin1String("timespent")).toInt();
    issue.originalEstimateSeconds = fields.value(QLatin1String("timeoriginalestimate")).toInt();
    return issue;
}

SearchResult SearchResult::fromJson(const QJsonObject &object)
{
    SearchResult result;
    result.startAt = object.value(QLatin1String("startAt")).toInt();
    result.maxResults = object.value(QLatin1String("maxResults")).toInt();
    // Jira Cloud has started omitting "total" on some search endpoints; fall
    // back to what we actually received so paging still behaves.
    const QJsonArray issues = object.value(QLatin1String("issues")).toArray();
    result.total = object.value(QLatin1String("total")).toInt(result.startAt + issues.size());
    result.issues.reserve(issues.size());
    for (const QJsonValue &entry : issues)
        result.issues.append(Issue::fromJson(entry.toObject()));
    return result;
}

QList<Project> Project::listFromJson(const QJsonValue &value)
{
    QList<Project> projects;
    // /project returns a bare array; /project/search wraps it in "values".
    QJsonArray array = value.toArray();
    if (array.isEmpty() && value.isObject())
        array = value.toObject().value(QLatin1String("values")).toArray();

    projects.reserve(array.size());
    for (const QJsonValue &entry : array) {
        const QJsonObject object = entry.toObject();
        Project project;
        project.id = stringAt(object, "id");
        project.key = stringAt(object, "key");
        project.name = stringAt(object, "name");
        if (!project.key.isEmpty())
            projects.append(project);
    }
    return projects;
}

QString Error::toString() const
{
    QString text = message;
    if (!details.isEmpty())
        text += QLatin1String("\n\n") + details.join(QLatin1Char('\n'));
    return text;
}

Error Error::fromResponse(int httpStatus, const QByteArray &body, const QString &transportError)
{
    Error error;
    error.httpStatus = httpStatus;

    const QJsonObject payload = QJsonDocument::fromJson(body).object();
    for (const QJsonValue &entry : payload.value(QLatin1String("errorMessages")).toArray()) {
        if (!entry.toString().isEmpty())
            error.details.append(entry.toString());
    }
    const QJsonObject fieldErrors = payload.value(QLatin1String("errors")).toObject();
    for (auto it = fieldErrors.begin(); it != fieldErrors.end(); ++it)
        error.details.append(QStringLiteral("%1: %2").arg(it.key(), it.value().toString()));

    // A Jira sitting behind SSO or a reverse proxy answers with an HTML login
    // page rather than JSON, so the status code is all we have to go on.
    switch (httpStatus) {
    case 401:
        error.message = QObject::tr("Jira rejected the credentials (401). Check the user name and API token, "
                                    "and that the authentication method matches your Jira type.");
        break;
    case 403:
        error.message = QObject::tr("Jira refused the request (403). The token may lack permission, or the account "
                                    "may need to clear a CAPTCHA by signing in through the browser once.");
        break;
    case 404:
        error.message = QObject::tr("Not found (404). Check the server URL -- a Jira served under a context path "
                                    "needs it included, for example https://jira.example.com/jira");
        break;
    case 429:
        error.message = QObject::tr("Rate limited by Jira (429). Wait a moment and try again.");
        break;
    default:
        break;
    }

    if (error.message.isEmpty()) {
        if (!transportError.isEmpty())
            error.message = transportError;
        else if (!error.details.isEmpty())
            error.message = error.details.takeFirst();
        else if (httpStatus > 0)
            error.message = QObject::tr("Jira returned HTTP %1.").arg(httpStatus);
        else
            error.message = QObject::tr("The request failed.");
    }
    return error;
}

} // namespace jira
