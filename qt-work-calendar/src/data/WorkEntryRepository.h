#ifndef WORKCALENDAR_WORKENTRYREPOSITORY_H
#define WORKCALENDAR_WORKENTRYREPOSITORY_H

#include "core/WorkEntry.h"
#include "data/Database.h"

#include <QDate>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

/*!
 * \brief Reads and writes work entries.
 *
 * All SQL for the entries table lives here; nothing above this layer knows a
 * column name. Every failing call leaves a sentence in \a error and returns
 * false, so the caller can show it without inventing its own wording.
 */
class WorkEntryRepository
{
public:
    explicit WorkEntryRepository(Database &database);

    /*! Inserts \a entry and fills in its id, createdAt and updatedAt. */
    bool add(WorkEntry &entry, QString *error = nullptr);

    /*! Writes \a entry back by id and refreshes its updatedAt. */
    bool update(WorkEntry &entry, QString *error = nullptr);

    /*!
     * \brief Inserts, or updates the row that carries the same external id.
     *
     * This is what makes re-importing a month from Jira safe: a worklog that
     * was already imported is refreshed rather than duplicated. Entries edited
     * by hand keep an empty external id and are never touched by an import.
     */
    bool addOrUpdateByExternalId(WorkEntry &entry, QString *error = nullptr);

    bool remove(int id, QString *error = nullptr);
    bool removeMany(const QVector<int> &ids, QString *error = nullptr);

    /*! The entry with \a id; \a found is set to false when there is none. */
    WorkEntry byId(int id, bool *found = nullptr) const;

    QVector<WorkEntry> forDate(const QDate &date) const;

    /*! Entries from \a from to \a to inclusive, ordered by date then start time. */
    QVector<WorkEntry> inRange(const QDate &from, const QDate &to) const;

    QVector<WorkEntry> all() const;

    /*! Total minutes per day in the range, for the calendar and the chart. */
    QMap<QDate, int> minutesByDate(const QDate &from, const QDate &to) const;

    /*! Number of stored entries, used by the status bar and the tests. */
    int count() const;

    /*! The earliest and latest dates that carry an entry. */
    QDate earliestDate() const;
    QDate latestDate() const;

    /*!
     * \brief Distinct non-empty values of \a field, most recent first.
     *
     * Feeds the filter combo boxes and the issue auto-completion, so the fields
     * people actually use are offered back to them.
     */
    QStringList distinctValues(WorkEntry::Field field, int limit = 200) const;

    /*!
     * \brief The most recently worked issues, newest first.
     *
     * Returned as whole entries so that picking one can prefill the summary,
     * sprint, fix version and links as well as the key.
     */
    QVector<WorkEntry> recentIssues(int limit = 15) const;

private:
    /*! Reads the current row of \a query into a WorkEntry. */
    static WorkEntry entryFromQuery(const class QSqlQuery &query);

    /*! Binds every column of \a entry onto a prepared insert or update. */
    static void bindEntry(class QSqlQuery &query, const WorkEntry &entry);

    /*! The column list shared by every select, in the order entryFromQuery expects. */
    static QString selectColumns();

    Database &m_database;
};

#endif // WORKCALENDAR_WORKENTRYREPOSITORY_H
