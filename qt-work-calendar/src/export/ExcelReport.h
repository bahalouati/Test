#ifndef WORKCALENDAR_EXCELREPORT_H
#define WORKCALENDAR_EXCELREPORT_H

#include "core/DaySummary.h"
#include "core/Settings.h"
#include "core/WorkEntry.h"

#include <QDate>
#include <QString>
#include <QVector>

/*!
 * \file ExcelReport.h
 * \brief Writes the workbook people are used to receiving.
 *
 * Three sheets:
 *  - \b Worklogs - one row per entry, filterable, with clickable links
 *  - \b Calendar - the month as a grid, each day coloured by how it went
 *  - \b Summary  - totals and the usual breakdowns
 */
namespace ExcelReport {

/*! What to put in the workbook. */
struct Options
{
    /*! The columns of the Worklogs sheet; empty means the default set. */
    QVector<WorkEntry::Field> columns;

    bool includeWorklogSheet = true;
    bool includeCalendarSheet = true;
    bool includeSummarySheet = true;

    /*! Shown on the Summary sheet; a sensible one is built when left empty. */
    QString title;
};

/*!
 * \brief Writes the report for \a days and the entries they were built from.
 *
 * \param path      destination file
 * \param entries   every entry of the period, in display order
 * \param days      one summary per calendar day of the period
 * \param settings  targets and colours
 * \param[out] error  why it failed, when it does
 */
bool write(const QString &path,
           const QVector<WorkEntry> &entries,
           const QVector<DaySummary> &days,
           const Settings &settings,
           const Options &options,
           QString *error = nullptr);

/*! The file name suggested for a report covering \a from to \a to. */
QString suggestedFileName(const QDate &from, const QDate &to);

} // namespace ExcelReport

#endif // WORKCALENDAR_EXCELREPORT_H
