#ifndef WORKCALENDAR_DAYMETA_H
#define WORKCALENDAR_DAYMETA_H

#include "core/Enums.h"

#include <QDate>
#include <QString>

/*!
 * \brief What a particular calendar day is, independently of the work logged.
 *
 * Only days that differ from the norm need a record: a plain working day has no
 * row in the database and is represented by a default-constructed DayMeta.
 * Without this, a public holiday would be painted red for having no hours, and
 * would drag the monthly balance down by a full day.
 */
struct DayMeta
{
    QDate date;
    DayType type = DayType::Workday;

    /*!
     * \brief Target for this day in minutes, or -1 to use the usual rule.
     *
     * Set it for the odd day that is agreed shorter or longer without being a
     * half day - a company event, a half-day of training, a long release night.
     */
    int targetMinutesOverride = -1;

    QString note; //!< free text shown in the day panel and the tooltip

    /*! True when the record carries nothing worth storing. */
    bool isEmpty() const;
};

#endif // WORKCALENDAR_DAYMETA_H
