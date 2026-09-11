#include "timesheet.h"

#include <QLocale>
#include <QObject>
#include <QMap>
#include <QSettings>

namespace jira {

namespace {
constexpr auto kTestSheetKey = "timesheet/testSheetMarker";
constexpr auto kMergeRequestKey = "timesheet/mergeRequestMarker";
constexpr auto kSprintFieldKey = "timesheet/sprintFieldId";
constexpr auto kFullDayKey = "timesheet/fullDayHours";
constexpr auto kPartialDayKey = "timesheet/partialDayHours";

// Hours print as "2.5" rather than "2.50" or "2,50" -- the script's :.1f, but
// without a trailing ".0" on whole hours.
QString formatHours(double hours)
{
    QString text = QString::number(hours, 'f', 2);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text;
}
} // namespace

QString TimesheetEntry::calendarLine() const
{
    return QStringLiteral("%1 [%2] [%3] (%4h)")
            .arg(issueKey,
                 sprint.isEmpty() ? QStringLiteral("-") : sprint,
                 fixVersions.isEmpty() ? QStringLiteral("-") : fixVersions,
                 formatHours(hours));
}

bool TimesheetRules::isWorkingDay(const QDate &day) const
{
    return day.isValid() && day.dayOfWeek() >= Qt::Monday && day.dayOfWeek() <= Qt::Friday;
}

QString dayStatusLabel(DayStatus status)
{
    switch (status) {
    case DayStatus::Future:
        return QObject::tr("Upcoming");
    case DayStatus::Complete:
        return QObject::tr("Complete");
    case DayStatus::Partial:
        return QObject::tr("Under a full day");
    case DayStatus::Short:
        return QObject::tr("Missing hours");
    }
    return {};
}

DayStatus classifyDay(const QDate &day, double hours, const QDate &today, const TimesheetRules &rules)
{
    // A day that has not happened is not missing anything, however empty it is.
    if (day.isValid() && today.isValid() && day > today)
        return DayStatus::Future;
    if (hours >= rules.fullDayHours)
        return DayStatus::Complete;
    if (hours >= rules.partialDayHours)
        return DayStatus::Partial;
    return DayStatus::Short;
}

double DaySummary::missingHours(const TimesheetRules &rules) const
{
    if (status == DayStatus::Future)
        return 0.0;
    const double shortfall = rules.fullDayHours - hours;
    return shortfall > 0.0 ? shortfall : 0.0;
}

QList<DaySummary> summariseDays(const QList<TimesheetEntry> &entries,
                                const QDate &from,
                                const QDate &to,
                                const QDate &today,
                                const TimesheetRules &rules)
{
    QMap<QDate, QList<TimesheetEntry>> byDay;
    for (const TimesheetEntry &entry : entries) {
        if (entry.day.isValid())
            byDay[entry.day].append(entry);
    }

    QList<DaySummary> days;
    if (!from.isValid() || !to.isValid() || from > to)
        return days;

    for (QDate day = from; day <= to; day = day.addDays(1)) {
        if (!rules.isWorkingDay(day))
            continue;

        DaySummary summary;
        summary.day = day;
        summary.entries = byDay.value(day);
        for (const TimesheetEntry &entry : summary.entries)
            summary.hours += entry.hours;
        summary.status = classifyDay(day, summary.hours, today, rules);
        days.append(summary);
    }
    return days;
}

double totalMissingHours(const QList<DaySummary> &days, const TimesheetRules &rules)
{
    double total = 0.0;
    for (const DaySummary &day : days)
        total += day.missingHours(rules);
    return total;
}

double totalLoggedHours(const QList<DaySummary> &days)
{
    double total = 0.0;
    for (const DaySummary &day : days)
        total += day.hours;
    return total;
}

TimesheetSettings TimesheetSettings::load()
{
    QSettings settings;
    TimesheetSettings loaded;
    loaded.testSheetMarker =
            settings.value(QLatin1String(kTestSheetKey), loaded.testSheetMarker).toString();
    loaded.mergeRequestMarker =
            settings.value(QLatin1String(kMergeRequestKey), loaded.mergeRequestMarker).toString();
    loaded.sprintFieldId =
            settings.value(QLatin1String(kSprintFieldKey), loaded.sprintFieldId).toString();
    loaded.rules.fullDayHours =
            settings.value(QLatin1String(kFullDayKey), loaded.rules.fullDayHours).toDouble();
    loaded.rules.partialDayHours =
            settings.value(QLatin1String(kPartialDayKey), loaded.rules.partialDayHours).toDouble();

    // A zero or inverted target would paint every day red; fall back rather
    // than show nonsense.
    if (loaded.rules.fullDayHours <= 0.0)
        loaded.rules.fullDayHours = 8.0;
    if (loaded.rules.partialDayHours < 0.0 || loaded.rules.partialDayHours > loaded.rules.fullDayHours)
        loaded.rules.partialDayHours = loaded.rules.fullDayHours * 0.75;
    return loaded;
}

void TimesheetSettings::save() const
{
    QSettings settings;
    settings.setValue(QLatin1String(kTestSheetKey), testSheetMarker);
    settings.setValue(QLatin1String(kMergeRequestKey), mergeRequestMarker);
    settings.setValue(QLatin1String(kSprintFieldKey), sprintFieldId);
    settings.setValue(QLatin1String(kFullDayKey), rules.fullDayHours);
    settings.setValue(QLatin1String(kPartialDayKey), rules.partialDayHours);
}

} // namespace jira
