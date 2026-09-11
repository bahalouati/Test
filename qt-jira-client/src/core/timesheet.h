#pragma once

#include "jiratypes.h"

#include <QDate>
#include <QList>
#include <QString>

// Turning my worklogs into "which days am I short?".
//
// This mirrors jira2.py: one row per worklog of mine, grouped by day, each day
// banded against a target so a thin day is visible without adding anything up.
namespace jira {

// One worklog of mine, resolved against the issue it belongs to. The row shape
// of the script's "Worklogs" sheet.
struct TimesheetEntry {
    QDate day;
    QString issueKey;
    QString summary;
    QString fixVersions;      // joined with ", "; empty when the issue has none
    QString sprint;
    QString mergeRequestUrl;
    QString testSheetName;
    QString testSheetUrl;
    QString comment;
    double hours = 0.0;

    bool hasFixVersion() const { return !fixVersions.isEmpty(); }
    bool hasSprint() const { return !sprint.isEmpty(); }
    bool hasMergeRequest() const { return !mergeRequestUrl.isEmpty(); }
    bool hasTestSheet() const { return !testSheetUrl.isEmpty(); }

    // "OPS-42 [Sprint 12] [1.4.0] (2.5h)" -- the calendar cell line.
    QString calendarLine() const;
};

// What the day is doing relative to the target.
enum class DayStatus {
    Future,     // hasn't happened yet -- never counts as missing
    Complete,   // at or above a full day
    Partial,    // logged something, but under a full day
    Short       // well under, including nothing at all
};

// The thresholds. Defaults match the script: 8h green, 6h yellow, else red.
struct TimesheetRules {
    double fullDayHours = 8.0;
    double partialDayHours = 6.0;

    // Weekends are excluded entirely, as the script's Mon-Fri calendar does.
    bool isWorkingDay(const QDate &day) const;
};

struct DaySummary {
    QDate day;
    double hours = 0.0;
    DayStatus status = DayStatus::Short;
    QList<TimesheetEntry> entries;

    // How far short of a full day this is; 0 for future days and full ones.
    double missingHours(const TimesheetRules &rules) const;
    bool isMissing(const TimesheetRules &rules) const { return missingHours(rules) > 0.0; }
};

QString dayStatusLabel(DayStatus status);

DayStatus classifyDay(const QDate &day, double hours, const QDate &today, const TimesheetRules &rules);

// One DaySummary per working day in [from, to] -- including days with nothing
// logged, which are exactly the ones worth seeing.
QList<DaySummary> summariseDays(const QList<TimesheetEntry> &entries,
                                const QDate &from,
                                const QDate &to,
                                const QDate &today,
                                const TimesheetRules &rules);

// Hours still owed across every past working day that is short.
double totalMissingHours(const QList<DaySummary> &days, const TimesheetRules &rules);
double totalLoggedHours(const QList<DaySummary> &days);

// Per-instance conventions. All of it is guesswork on a fresh install, so every
// one of these is editable and persisted.
struct TimesheetSettings {
    // Substring that marks an attachment as the test sheet.
    QString testSheetMarker = QStringLiteral("testsheet");
    // Substring that marks a remote link as the merge request.
    QString mergeRequestMarker = QStringLiteral("/merge_requests/");
    // Extra fields to request; the sprint field id goes here. Empty means the
    // client scans every customfield_* for something sprint-shaped instead.
    QString sprintFieldId = QStringLiteral("customfield_10005");
    TimesheetRules rules;

    static TimesheetSettings load();
    void save() const;
};

} // namespace jira
