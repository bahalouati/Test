#ifndef WORKCALENDAR_STATISTICS_H
#define WORKCALENDAR_STATISTICS_H

#include "core/DayMeta.h"
#include "core/DaySummary.h"
#include "core/Settings.h"
#include "core/WorkEntry.h"

#include <QDate>
#include <QHash>
#include <QString>
#include <QVector>

/*!
 * \file Statistics.h
 * \brief Turns stored rows into the numbers the calendar and reports show.
 *
 * Every function here is pure: it takes the data it needs as arguments and
 * touches neither the database nor the clock. That keeps the arithmetic - which
 * is the part people will argue with - testable without a database.
 */
namespace Statistics {

/*! One grouped line of a report: "PLAT: 14h 30m over 9 entries". */
struct BreakdownRow
{
    QString key;             //!< the grouping value, or "(none)" when empty
    int minutes = 0;
    int billableMinutes = 0;
    int entryCount = 0;
    double share = 0.0;      //!< fraction of the period total, 0..1
};

/*! The headline numbers for a period. */
struct PeriodTotals
{
    QDate from;
    QDate to;

    int totalMinutes = 0;
    int billableMinutes = 0;
    int targetMinutes = 0;   //!< sum of the targets of every day in the period
    int entryCount = 0;

    int daysWithWork = 0;    //!< days carrying at least one entry
    int expectedDays = 0;    //!< days that carry a target
    int completeDays = 0;    //!< days that reached their target
    int shortDays = 0;       //!< past days that expected work and fell short
    int absenceDays = 0;     //!< holidays, vacation, sick leave

    int longestCompleteStreak = 0; //!< consecutive expected days that reached target
    int currentCompleteStreak = 0; //!< the streak ending at the last past day

    /*! Logged minus expected: positive is overtime, negative is a deficit. */
    int balanceMinutes() const;

    /*! Average over the days that carry work, not over the calendar. */
    int averageMinutesPerWorkedDay() const;

    /*! Billable share of the logged time, 0..1. */
    double billableShare() const;
};

/*!
 * \brief Builds one DaySummary per day between \a from and \a to inclusive.
 *
 * \param entries   every entry in the range, in any order
 * \param dayMeta   the stored day records; missing days get defaults
 * \param settings  supplies the targets and the working-day pattern
 */
QVector<DaySummary> buildDaySummaries(const QDate &from,
                                      const QDate &to,
                                      const QVector<WorkEntry> &entries,
                                      const QHash<QDate, DayMeta> &dayMeta,
                                      const Settings &settings);

/*!
 * \brief Adds the summaries up.
 * \param today  days after it are not counted as short, since they are not due
 */
PeriodTotals totals(const QVector<DaySummary> &days, const QDate &today);

/*!
 * \brief Groups \a entries by \a field and sorts the result by time spent.
 *
 * Only fields that make sense as a group are supported (project, activity,
 * issue, sprint, ...); anything else yields an empty list.
 */
QVector<BreakdownRow> breakdownBy(const QVector<WorkEntry> &entries, WorkEntry::Field field);

/*! The fields the reports offer as grouping choices. */
QVector<WorkEntry::Field> groupableFields();

} // namespace Statistics

#endif // WORKCALENDAR_STATISTICS_H
