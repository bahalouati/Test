#ifndef WORKCALENDAR_DAYMETAREPOSITORY_H
#define WORKCALENDAR_DAYMETAREPOSITORY_H

#include "core/DayMeta.h"
#include "data/Database.h"

#include <QDate>
#include <QHash>
#include <QVector>

/*!
 * \brief Reads and writes the per-day records (holidays, half days, notes).
 *
 * Only days that differ from a normal working day are stored. Asking for a day
 * that has no row returns a default DayMeta carrying that date, so callers
 * never have to test for absence.
 */
class DayMetaRepository
{
public:
    explicit DayMetaRepository(Database &database);

    /*! The record for \a date, or a default one when nothing is stored. */
    DayMeta forDate(const QDate &date) const;

    /*! Every stored record between \a from and \a to, keyed by date. */
    QHash<QDate, DayMeta> inRange(const QDate &from, const QDate &to) const;

    /*!
     * \brief Stores \a meta, or deletes the row when it holds nothing special.
     *
     * Keeping empty records out of the table means "reset this day" needs no
     * separate command.
     */
    bool save(const DayMeta &meta, QString *error = nullptr);

    bool remove(const QDate &date, QString *error = nullptr);

    /*! Days in the range that are absences, for the statistics. */
    int absenceDayCount(const QDate &from, const QDate &to) const;

private:
    Database &m_database;
};

#endif // WORKCALENDAR_DAYMETAREPOSITORY_H
