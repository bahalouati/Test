#include "data/WorkEntryRepository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

/*!
 * \brief Turns a null QString into an empty one.
 *
 * QSqlQuery binds a null QString as SQL NULL, and every text column here is
 * declared NOT NULL - so an entry with, say, no branch would be rejected
 * outright. Normalising here keeps the schema strict and the caller free to
 * leave any optional field untouched.
 */
QString text(const QString &value)
{
    return value.isNull() ? QString(QLatin1String("")) : value;
}

/*! "" for a null time, "HH:mm" otherwise; the database never stores nulls. */
QString timeToText(const QTime &time)
{
    return time.isValid() ? time.toString(QStringLiteral("HH:mm")) : QString();
}

QTime timeFromText(const QString &text)
{
    return text.isEmpty() ? QTime() : QTime::fromString(text, QStringLiteral("HH:mm"));
}

QString dateTimeToText(const QDateTime &dateTime)
{
    return dateTime.isValid() ? dateTime.toString(Qt::ISODate) : QString();
}

QDateTime dateTimeFromText(const QString &text)
{
    return text.isEmpty() ? QDateTime() : QDateTime::fromString(text, Qt::ISODate);
}

/*! Records the SQL problem in \a error, if the caller asked for it. */
bool fail(const QSqlQuery &query, QString *error)
{
    if (error)
        *error = query.lastError().text();
    return false;
}

} // namespace

WorkEntryRepository::WorkEntryRepository(Database &database)
    : m_database(database)
{
}

QString WorkEntryRepository::selectColumns()
{
    return QStringLiteral(
        "id, date, start_time, end_time, minutes, issue_key, summary, description, project,"
        "issue_type, epic, sprint, component, fix_version, branch, merge_request,"
        "testsheet_name, testsheet_url, issue_url, tags, activity, status, priority, location,"
        "billable, source, external_id, created_at, updated_at");
}

WorkEntry WorkEntryRepository::entryFromQuery(const QSqlQuery &query)
{
    WorkEntry entry;
    int column = 0;
    entry.id            = query.value(column++).toInt();
    entry.date          = QDate::fromString(query.value(column++).toString(), Qt::ISODate);
    entry.startTime     = timeFromText(query.value(column++).toString());
    entry.endTime       = timeFromText(query.value(column++).toString());
    entry.minutes       = query.value(column++).toInt();
    entry.issueKey      = query.value(column++).toString();
    entry.summary       = query.value(column++).toString();
    entry.description   = query.value(column++).toString();
    entry.project       = query.value(column++).toString();
    entry.issueType     = query.value(column++).toString();
    entry.epic          = query.value(column++).toString();
    entry.sprint        = query.value(column++).toString();
    entry.component     = query.value(column++).toString();
    entry.fixVersion    = query.value(column++).toString();
    entry.branch        = query.value(column++).toString();
    entry.mergeRequest  = query.value(column++).toString();
    entry.testsheetName = query.value(column++).toString();
    entry.testsheetUrl  = query.value(column++).toString();
    entry.issueUrl      = query.value(column++).toString();
    entry.tags          = query.value(column++).toString();
    entry.activity      = activityFromString(query.value(column++).toString());
    entry.status        = entryStatusFromString(query.value(column++).toString());
    entry.priority      = priorityFromString(query.value(column++).toString());
    entry.location      = workLocationFromString(query.value(column++).toString());
    entry.billable      = query.value(column++).toInt() != 0;
    entry.source        = entrySourceFromString(query.value(column++).toString());
    entry.externalId    = query.value(column++).toString();
    entry.createdAt     = dateTimeFromText(query.value(column++).toString());
    entry.updatedAt     = dateTimeFromText(query.value(column++).toString());
    return entry;
}

void WorkEntryRepository::bindEntry(QSqlQuery &query, const WorkEntry &entry)
{
    query.bindValue(QStringLiteral(":date"),           entry.date.toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":start_time"),     text(timeToText(entry.startTime)));
    query.bindValue(QStringLiteral(":end_time"),       text(timeToText(entry.endTime)));
    query.bindValue(QStringLiteral(":minutes"),        entry.minutes);
    query.bindValue(QStringLiteral(":issue_key"),      text(entry.issueKey));
    query.bindValue(QStringLiteral(":summary"),        text(entry.summary));
    query.bindValue(QStringLiteral(":description"),    text(entry.description));
    query.bindValue(QStringLiteral(":project"),        text(entry.project));
    query.bindValue(QStringLiteral(":issue_type"),     text(entry.issueType));
    query.bindValue(QStringLiteral(":epic"),           text(entry.epic));
    query.bindValue(QStringLiteral(":sprint"),         text(entry.sprint));
    query.bindValue(QStringLiteral(":component"),      text(entry.component));
    query.bindValue(QStringLiteral(":fix_version"),    text(entry.fixVersion));
    query.bindValue(QStringLiteral(":branch"),         text(entry.branch));
    query.bindValue(QStringLiteral(":merge_request"),  text(entry.mergeRequest));
    query.bindValue(QStringLiteral(":testsheet_name"), text(entry.testsheetName));
    query.bindValue(QStringLiteral(":testsheet_url"),  text(entry.testsheetUrl));
    query.bindValue(QStringLiteral(":issue_url"),      text(entry.issueUrl));
    query.bindValue(QStringLiteral(":tags"),           text(entry.tags));
    query.bindValue(QStringLiteral(":activity"),       activityToString(entry.activity));
    query.bindValue(QStringLiteral(":status"),         entryStatusToString(entry.status));
    query.bindValue(QStringLiteral(":priority"),       priorityToString(entry.priority));
    query.bindValue(QStringLiteral(":location"),       workLocationToString(entry.location));
    query.bindValue(QStringLiteral(":billable"),       entry.billable ? 1 : 0);
    query.bindValue(QStringLiteral(":source"),         entrySourceToString(entry.source));
    query.bindValue(QStringLiteral(":external_id"),    text(entry.externalId));
    query.bindValue(QStringLiteral(":created_at"),     text(dateTimeToText(entry.createdAt)));
    query.bindValue(QStringLiteral(":updated_at"),     text(dateTimeToText(entry.updatedAt)));
}

bool WorkEntryRepository::add(WorkEntry &entry, QString *error)
{
    if (!entry.isValid(error))
        return false;

    const QDateTime now = QDateTime::currentDateTime();
    if (!entry.createdAt.isValid())
        entry.createdAt = now;
    entry.updatedAt = now;

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "INSERT INTO entries ("
        "date, start_time, end_time, minutes, issue_key, summary, description, project,"
        "issue_type, epic, sprint, component, fix_version, branch, merge_request,"
        "testsheet_name, testsheet_url, issue_url, tags, activity, status, priority, location,"
        "billable, source, external_id, created_at, updated_at) "
        "VALUES ("
        ":date, :start_time, :end_time, :minutes, :issue_key, :summary, :description, :project,"
        ":issue_type, :epic, :sprint, :component, :fix_version, :branch, :merge_request,"
        ":testsheet_name, :testsheet_url, :issue_url, :tags, :activity, :status, :priority, :location,"
        ":billable, :source, :external_id, :created_at, :updated_at)"));
    bindEntry(query, entry);

    if (!query.exec())
        return fail(query, error);

    entry.id = query.lastInsertId().toInt();
    return true;
}

bool WorkEntryRepository::update(WorkEntry &entry, QString *error)
{
    if (entry.id < 0) {
        if (error)
            *error = QStringLiteral("This entry has never been saved, so it cannot be updated.");
        return false;
    }
    if (!entry.isValid(error))
        return false;

    entry.updatedAt = QDateTime::currentDateTime();

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "UPDATE entries SET "
        "date = :date, start_time = :start_time, end_time = :end_time, minutes = :minutes,"
        "issue_key = :issue_key, summary = :summary, description = :description,"
        "project = :project, issue_type = :issue_type, epic = :epic, sprint = :sprint,"
        "component = :component, fix_version = :fix_version, branch = :branch,"
        "merge_request = :merge_request, testsheet_name = :testsheet_name,"
        "testsheet_url = :testsheet_url, issue_url = :issue_url, tags = :tags,"
        "activity = :activity, status = :status, priority = :priority, location = :location,"
        "billable = :billable, source = :source, external_id = :external_id,"
        "created_at = :created_at, updated_at = :updated_at "
        "WHERE id = :id"));
    bindEntry(query, entry);
    query.bindValue(QStringLiteral(":id"), entry.id);

    if (!query.exec())
        return fail(query, error);
    return true;
}

bool WorkEntryRepository::addOrUpdateByExternalId(WorkEntry &entry, QString *error)
{
    if (entry.externalId.trimmed().isEmpty())
        return add(entry, error);

    QSqlQuery lookup(m_database.connection());
    lookup.prepare(QStringLiteral("SELECT id, created_at FROM entries WHERE external_id = :external_id"));
    lookup.bindValue(QStringLiteral(":external_id"), entry.externalId);
    if (!lookup.exec())
        return fail(lookup, error);

    if (lookup.next()) {
        entry.id = lookup.value(0).toInt();
        entry.createdAt = dateTimeFromText(lookup.value(1).toString());
        return update(entry, error);
    }
    return add(entry, error);
}

bool WorkEntryRepository::remove(int id, QString *error)
{
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral("DELETE FROM entries WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return fail(query, error);
    return true;
}

bool WorkEntryRepository::removeMany(const QVector<int> &ids, QString *error)
{
    if (ids.isEmpty())
        return true;

    QSqlDatabase database = m_database.connection();
    database.transaction();
    for (int id : ids) {
        if (!remove(id, error)) {
            database.rollback();
            return false;
        }
    }
    if (!database.commit()) {
        if (error)
            *error = database.lastError().text();
        return false;
    }
    return true;
}

WorkEntry WorkEntryRepository::byId(int id, bool *found) const
{
    if (found)
        *found = false;

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral("SELECT %1 FROM entries WHERE id = :id").arg(selectColumns()));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
        return WorkEntry();

    if (found)
        *found = true;
    return entryFromQuery(query);
}

QVector<WorkEntry> WorkEntryRepository::forDate(const QDate &date) const
{
    return inRange(date, date);
}

QVector<WorkEntry> WorkEntryRepository::inRange(const QDate &from, const QDate &to) const
{
    QVector<WorkEntry> entries;
    if (!from.isValid() || !to.isValid())
        return entries;

    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "SELECT %1 FROM entries WHERE date BETWEEN :from AND :to "
        "ORDER BY date, CASE WHEN start_time = '' THEN 1 ELSE 0 END, start_time, id")
                      .arg(selectColumns()));
    query.bindValue(QStringLiteral(":from"), from.toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":to"), to.toString(Qt::ISODate));
    if (!query.exec())
        return entries;

    while (query.next())
        entries.append(entryFromQuery(query));
    return entries;
}

QVector<WorkEntry> WorkEntryRepository::all() const
{
    QVector<WorkEntry> entries;
    QSqlQuery query(m_database.connection());
    if (!query.exec(QStringLiteral("SELECT %1 FROM entries ORDER BY date, start_time, id")
                        .arg(selectColumns()))) {
        return entries;
    }
    while (query.next())
        entries.append(entryFromQuery(query));
    return entries;
}

QMap<QDate, int> WorkEntryRepository::minutesByDate(const QDate &from, const QDate &to) const
{
    QMap<QDate, int> totals;
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "SELECT date, SUM(minutes) FROM entries WHERE date BETWEEN :from AND :to GROUP BY date"));
    query.bindValue(QStringLiteral(":from"), from.toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":to"), to.toString(Qt::ISODate));
    if (!query.exec())
        return totals;

    while (query.next()) {
        const QDate date = QDate::fromString(query.value(0).toString(), Qt::ISODate);
        if (date.isValid())
            totals.insert(date, query.value(1).toInt());
    }
    return totals;
}

int WorkEntryRepository::count() const
{
    QSqlQuery query(m_database.connection());
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM entries")) || !query.next())
        return 0;
    return query.value(0).toInt();
}

QDate WorkEntryRepository::earliestDate() const
{
    QSqlQuery query(m_database.connection());
    if (!query.exec(QStringLiteral("SELECT MIN(date) FROM entries")) || !query.next())
        return QDate();
    return QDate::fromString(query.value(0).toString(), Qt::ISODate);
}

QDate WorkEntryRepository::latestDate() const
{
    QSqlQuery query(m_database.connection());
    if (!query.exec(QStringLiteral("SELECT MAX(date) FROM entries")) || !query.next())
        return QDate();
    return QDate::fromString(query.value(0).toString(), Qt::ISODate);
}

QStringList WorkEntryRepository::distinctValues(WorkEntry::Field field, int limit) const
{
    // Only the free-text columns are worth offering as filters; the enums
    // already have a fixed list of their own.
    QString column;
    switch (field) {
    case WorkEntry::FieldIssueKey:   column = QStringLiteral("issue_key"); break;
    case WorkEntry::FieldProject:    column = QStringLiteral("project"); break;
    case WorkEntry::FieldIssueType:  column = QStringLiteral("issue_type"); break;
    case WorkEntry::FieldEpic:       column = QStringLiteral("epic"); break;
    case WorkEntry::FieldSprint:     column = QStringLiteral("sprint"); break;
    case WorkEntry::FieldComponent:  column = QStringLiteral("component"); break;
    case WorkEntry::FieldFixVersion: column = QStringLiteral("fix_version"); break;
    case WorkEntry::FieldBranch:     column = QStringLiteral("branch"); break;
    case WorkEntry::FieldTags:       column = QStringLiteral("tags"); break;
    default:
        return QStringList();
    }

    QStringList values;
    QSqlQuery query(m_database.connection());
    query.prepare(QStringLiteral(
        "SELECT %1, MAX(date) AS last_used FROM entries WHERE %1 <> '' "
        "GROUP BY %1 ORDER BY last_used DESC LIMIT :limit").arg(column));
    query.bindValue(QStringLiteral(":limit"), qMax(1, limit));
    if (!query.exec())
        return values;

    while (query.next())
        values.append(query.value(0).toString());
    return values;
}

QVector<WorkEntry> WorkEntryRepository::recentIssues(int limit) const
{
    QVector<WorkEntry> entries;
    QSqlQuery query(m_database.connection());

    // One row per issue key: the most recent entry of each, newest first.
    query.prepare(QStringLiteral(
        "SELECT %1 FROM entries WHERE id IN ("
        "  SELECT MAX(id) FROM entries WHERE issue_key <> '' GROUP BY issue_key"
        ") ORDER BY date DESC, id DESC LIMIT :limit").arg(selectColumns()));
    query.bindValue(QStringLiteral(":limit"), qMax(1, limit));
    if (!query.exec())
        return entries;

    while (query.next())
        entries.append(entryFromQuery(query));
    return entries;
}
