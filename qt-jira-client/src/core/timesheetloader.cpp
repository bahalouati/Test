#include "timesheetloader.h"

#include <QJsonObject>
#include <QJsonValue>

#include <utility>

namespace jira {

namespace {
// Enough to keep the connection busy without a self-inflicted rate limit on a
// shared instance.
constexpr int kMaxInFlight = 6;
constexpr int kSearchPageSize = 500;

QString jqlQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}
} // namespace

TimesheetLoader::TimesheetLoader(Client *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

QString TimesheetLoader::buildJql(const QDate &from, const QDate &to, const User &me)
{
    // worklogAuthor takes a user name on Server/DC and an accountId on Cloud;
    // currentUser() sidesteps the difference when we know who we are.
    QString author = QStringLiteral("currentUser()");
    if (!me.accountId.isEmpty())
        author = jqlQuote(me.accountId);
    else if (!me.name.isEmpty())
        author = jqlQuote(me.name);

    return QStringLiteral("worklogAuthor = %1 AND worklogDate >= %2 AND worklogDate <= %3")
            .arg(author,
                 jqlQuote(from.toString(Qt::ISODate)),
                 jqlQuote(to.toString(Qt::ISODate)));
}

bool TimesheetLoader::isMine(const User &author) const
{
    if (m_me.isNull())
        return true;   // no identity to compare against; trust the JQL filter
    if (!m_me.accountId.isEmpty() && !author.accountId.isEmpty())
        return author.accountId == m_me.accountId;
    if (!m_me.name.isEmpty() && !author.name.isEmpty())
        return author.name.compare(m_me.name, Qt::CaseInsensitive) == 0;
    if (!m_me.emailAddress.isEmpty() && !author.emailAddress.isEmpty())
        return author.emailAddress.compare(m_me.emailAddress, Qt::CaseInsensitive) == 0;
    return author.displayName == m_me.displayName;
}

void TimesheetLoader::cancel()
{
    m_running = false;
    m_queue.clear();
    m_pending.clear();
    m_inFlight = 0;
    m_completed = 0;
}

void TimesheetLoader::load(const QDate &from, const QDate &to, const User &me, const TimesheetSettings &settings)
{
    cancel();

    m_from = from;
    m_to = to;
    m_me = me;
    m_settings = settings;
    m_running = true;

    QStringList extraFields = {QStringLiteral("fixVersions"), QStringLiteral("attachment")};
    if (!settings.sprintFieldId.trimmed().isEmpty())
        extraFields.append(settings.sprintFieldId.trimmed());

    emit progress(0, 0, tr("Searching for issues you booked time against…"));

    Reply *reply = m_client->search(buildJql(from, to, me), 0, kSearchPageSize, extraFields);
    connect(reply, &Reply::succeeded, this, [this](const QJsonValue &body) {
        if (!m_running)
            return;
        const SearchResult result = SearchResult::fromJson(body.toObject());

        m_pending.reserve(result.issues.size());
        for (const Issue &issue : result.issues) {
            PendingIssue entry;
            entry.issue = issue;
            m_pending.append(entry);
        }

        if (m_pending.isEmpty()) {
            m_running = false;
            emit finished({});
            return;
        }

        for (int index = 0; index < m_pending.size(); ++index)
            m_queue.enqueue(index);
        emit progress(0, m_pending.size(), tr("Reading work logs…"));
        pumpQueue();
    });
    connect(reply, &Reply::failed, this, [this](const Error &error) {
        if (!m_running)
            return;
        m_running = false;
        emit failed(error.toString());
    });
}

void TimesheetLoader::pumpQueue()
{
    while (m_running && m_inFlight < kMaxInFlight && !m_queue.isEmpty())
        startIssue(m_queue.dequeue());

    if (m_running && m_inFlight == 0 && m_queue.isEmpty()) {
        // Everything has settled -- flatten it into rows.
        QList<TimesheetEntry> entries;
        for (const PendingIssue &pending : std::as_const(m_pending)) {
            const Attachment testSheet =
                    findAttachment(pending.issue.attachments, m_settings.testSheetMarker);

            for (const Worklog &worklog : pending.worklogs) {
                if (!isMine(worklog.author))
                    continue;
                const QDate day = worklog.started.toLocalTime().date();
                if (!day.isValid() || day < m_from || day > m_to)
                    continue;

                TimesheetEntry entry;
                entry.day = day;
                entry.issueKey = pending.issue.key;
                entry.summary = pending.issue.summary;
                QStringList releases;
                QStringList specifications;
                splitFixVersions(pending.issue.fixVersions, &releases, &specifications);
                entry.fixVersions = releases.join(QStringLiteral(", "));
                entry.specifications = specifications.join(QStringLiteral(", "));
                entry.sprint = pending.issue.sprint;
                entry.mergeRequestUrl = pending.mergeRequestUrl;
                entry.testSheetName = testSheet.filename;
                entry.testSheetUrl = testSheet.contentUrl;
                entry.comment = worklog.comment;
                entry.hours = worklog.timeSpentSeconds / 3600.0;
                entries.append(entry);
            }
        }

        m_running = false;
        emit finished(entries);
    }
}

void TimesheetLoader::startIssue(int index)
{
    if (index < 0 || index >= m_pending.size())
        return;

    const QString key = m_pending.at(index).issue.key;
    ++m_inFlight;

    Reply *worklogReply = m_client->fetchWorklogs(key);
    connect(worklogReply, &Reply::succeeded, this, [this, index](const QJsonValue &body) {
        if (!m_running || index >= m_pending.size())
            return;
        m_pending[index].worklogs = Worklog::listFromJson(body.toObject());
        m_pending[index].worklogsDone = true;
        issueSettled(index);
    });
    connect(worklogReply, &Reply::failed, this, [this, index](const Error &) {
        // One unreadable issue must not sink the whole month; it contributes
        // no hours, which shows up as a thinner day rather than an error.
        if (!m_running || index >= m_pending.size())
            return;
        m_pending[index].worklogsDone = true;
        issueSettled(index);
    });

    if (m_settings.mergeRequestMarker.trimmed().isEmpty()) {
        m_pending[index].linksDone = true;
        return;
    }

    Reply *linkReply = m_client->fetchRemoteLinks(key);
    connect(linkReply, &Reply::succeeded, this, [this, index](const QJsonValue &body) {
        if (!m_running || index >= m_pending.size())
            return;
        m_pending[index].mergeRequestUrl =
                findLinkUrl(RemoteLink::listFromJson(body), m_settings.mergeRequestMarker);
        m_pending[index].linksDone = true;
        issueSettled(index);
    });
    connect(linkReply, &Reply::failed, this, [this, index](const Error &) {
        // Remote links are frequently forbidden for non-admins; absence of a
        // merge request link is not an error worth surfacing.
        if (!m_running || index >= m_pending.size())
            return;
        m_pending[index].linksDone = true;
        issueSettled(index);
    });
}

void TimesheetLoader::issueSettled(int index)
{
    const PendingIssue &pending = m_pending.at(index);
    if (!pending.worklogsDone || !pending.linksDone)
        return;

    --m_inFlight;
    ++m_completed;
    emit progress(m_completed, m_pending.size(), tr("Reading work logs…"));
    pumpQueue();
}

} // namespace jira
