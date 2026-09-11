#pragma once

#include "timesheet.h"

#include <QDate>
#include <QList>
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

} // namespace jira
