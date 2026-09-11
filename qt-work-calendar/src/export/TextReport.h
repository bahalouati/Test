#ifndef WORKCALENDAR_TEXTREPORT_H
#define WORKCALENDAR_TEXTREPORT_H

#include "core/DaySummary.h"
#include "core/Settings.h"
#include "core/WorkEntry.h"

#include <QString>
#include <QVector>

/*!
 * \file TextReport.h
 * \brief Plain-text renderings meant for the clipboard.
 *
 * The daily stand-up note and the weekly recap are things people retype every
 * morning; the data is already here, so the application writes them.
 */
namespace TextReport {

/*! A stand-up note for \a day: what was done, and how long it took. */
QString standupNote(const DaySummary &day, const QVector<WorkEntry> &entries);

/*! A period recap: totals, balance and the time per project. */
QString periodRecap(const QVector<DaySummary> &days,
                    const QVector<WorkEntry> &entries,
                    const Settings &settings);

/*! The detail of one entry as rich text, for the day panel. */
QString entryDetailHtml(const WorkEntry &entry, const Settings &settings);

} // namespace TextReport

#endif // WORKCALENDAR_TEXTREPORT_H
