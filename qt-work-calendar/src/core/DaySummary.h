#ifndef WORKCALENDAR_DAYSUMMARY_H
#define WORKCALENDAR_DAYSUMMARY_H

#include "core/DayMeta.h"

#include <QDate>
#include <QString>
#include <QVector>

/*!
 * \brief One line of a calendar cell: the label of an entry and its duration.
 *
 * The grid paints these instead of holding whole WorkEntry objects, which keeps
 * the widget independent of the storage layer.
 */
struct DayLine
{
    QString label;    //!< "ABC-123 Fix the parser"
    QString detail;   //!< sprint, fix version, ... shown when the cell is tall enough
    int minutes = 0;
    bool billable = true;
};

/*!
 * \brief Everything the calendar needs to draw and judge a single day.
 */
struct DaySummary
{
    /*! How a day compares with its target; decides the cell colour. */
    enum State {
        Complete,   //!< target reached
        Partial,    //!< above the warning threshold but below target
        Low,        //!< some work logged, but well under target
        Empty,      //!< a working day in the past with nothing logged
        Future,     //!< after today: nothing is expected yet
        Absence,    //!< holiday, vacation, sick leave
        NonWorking  //!< weekend or a day outside the configured working days
    };

    QDate date;
    DayMeta meta;

    int totalMinutes = 0;
    int billableMinutes = 0;
    int targetMinutes = 0;       //!< 0 when nothing is expected on this day
    int entryCount = 0;

    QVector<DayLine> lines;

    /*! Minutes above (positive) or below (negative) the target. */
    int balanceMinutes() const;

    /*! Fraction of the target that is covered, clamped to [0, 1]. */
    double completion() const;

    /*!
     * \brief Classifies the day.
     * \param today     the reference "now", so tests need not depend on the clock
     * \param warningThresholdMinutes  at or above this, an incomplete day is Partial
     */
    State state(const QDate &today, int warningThresholdMinutes) const;

    /*! A one-line human description, used in tooltips and the status bar. */
    QString shortDescription() const;
};

#endif // WORKCALENDAR_DAYSUMMARY_H
