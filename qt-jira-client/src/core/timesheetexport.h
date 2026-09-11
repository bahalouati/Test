#pragma once

#include "timesheet.h"

#include <QDate>
#include <QList>
#include <QByteArray>
#include <QString>

namespace jira {

// Writes the two-sheet workbook the Python report produces: a flat "Worklogs"
// sheet and a colour-banded "Calendar" sheet for the month.
bool exportTimesheetWorkbook(const QString &path,
                             const QList<TimesheetEntry> &entries,
                             const QList<DaySummary> &days,
                             const QDate &month,
                             const TimesheetRules &rules,
                             QString *errorMessage = nullptr);

// "Jira_Worklog_Calendar_2026_09.xlsx"
QString suggestedWorkbookName(const QDate &month);

// The same rows as a CSV, ready to write. Starts with a UTF-8 byte order mark,
// without which Excel reads the file as the system code page and turns anything
// non-ASCII into mojibake.
QByteArray buildTimesheetCsv(const QList<TimesheetEntry> &entries);

} // namespace jira
