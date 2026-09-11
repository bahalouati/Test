#include "data/Statistics.h"

#include <QMap>
#include <algorithm>

namespace {

/*! Sorts breakdown rows by time spent, longest first, then alphabetically. */
bool breakdownRowIsBefore(const Statistics::BreakdownRow &left,
                          const Statistics::BreakdownRow &right)
{
    if (left.minutes != right.minutes)
        return left.minutes > right.minutes;
    return left.key.localeAwareCompare(right.key) < 0;
}

/*! The label an entry contributes to when grouped by \a field. */
QString groupKeyFor(const WorkEntry &entry, WorkEntry::Field field)
{
    QString key = entry.field(field).toString().trimmed();
    if (field == WorkEntry::FieldIssueKey && !key.isEmpty() && !entry.summary.trimmed().isEmpty())
        key = entry.title();
    if (key.isEmpty())
        key = QStringLiteral("(none)");
    return key;
}

/*! The extra line of context the calendar shows under an entry label. */
QString detailFor(const WorkEntry &entry)
{
    QStringList parts;
    if (!entry.sprint.trimmed().isEmpty())
        parts.append(entry.sprint.trimmed());
    if (!entry.fixVersion.trimmed().isEmpty())
        parts.append(entry.fixVersion.trimmed());
    if (parts.isEmpty())
        parts.append(activityDisplayName(entry.activity));
    return parts.join(QStringLiteral(" | "));
}

} // namespace

int Statistics::PeriodTotals::balanceMinutes() const
{
    return totalMinutes - targetMinutes;
}

int Statistics::PeriodTotals::averageMinutesPerWorkedDay() const
{
    if (daysWithWork <= 0)
        return 0;
    return totalMinutes / daysWithWork;
}

double Statistics::PeriodTotals::billableShare() const
{
    if (totalMinutes <= 0)
        return 0.0;
    return static_cast<double>(billableMinutes) / static_cast<double>(totalMinutes);
}

QVector<DaySummary> Statistics::buildDaySummaries(const QDate &from,
                                                  const QDate &to,
                                                  const QVector<WorkEntry> &entries,
                                                  const QHash<QDate, DayMeta> &dayMeta,
                                                  const Settings &settings)
{
    QVector<DaySummary> days;
    if (!from.isValid() || !to.isValid() || from > to)
        return days;

    // One empty summary per calendar day first, so days with no work still
    // exist in the result and can be judged against their target.
    QHash<QDate, int> indexByDate;
    for (QDate date = from; date <= to; date = date.addDays(1)) {
        DaySummary summary;
        summary.date = date;
        summary.meta = dayMeta.value(date);
        summary.meta.date = date;
        summary.targetMinutes = settings.targetMinutesFor(date, summary.meta);
        indexByDate.insert(date, days.size());
        days.append(summary);
    }

    for (const WorkEntry &entry : entries) {
        const int index = indexByDate.value(entry.date, -1);
        if (index < 0)
            continue; // outside the requested range

        DaySummary &summary = days[index];
        summary.totalMinutes += entry.minutes;
        if (entry.billable)
            summary.billableMinutes += entry.minutes;
        ++summary.entryCount;

        DayLine line;
        line.label = entry.title();
        line.detail = detailFor(entry);
        line.minutes = entry.minutes;
        line.billable = entry.billable;
        summary.lines.append(line);
    }

    return days;
}

Statistics::PeriodTotals Statistics::totals(const QVector<DaySummary> &days, const QDate &today)
{
    PeriodTotals result;
    if (days.isEmpty())
        return result;

    result.from = days.first().date;
    result.to = days.last().date;

    int streak = 0;
    for (const DaySummary &day : days) {
        result.totalMinutes += day.totalMinutes;
        result.billableMinutes += day.billableMinutes;
        result.targetMinutes += day.targetMinutes;
        result.entryCount += day.entryCount;

        if (day.totalMinutes > 0)
            ++result.daysWithWork;
        if (!dayTypeExpectsWork(day.meta.type))
            ++result.absenceDays;

        if (day.targetMinutes <= 0)
            continue; // nothing expected: neither a streak nor a shortfall

        ++result.expectedDays;

        if (day.totalMinutes >= day.targetMinutes) {
            ++result.completeDays;
            ++streak;
            result.longestCompleteStreak = qMax(result.longestCompleteStreak, streak);
            if (day.date <= today)
                result.currentCompleteStreak = streak;
        } else if (day.date <= today) {
            ++result.shortDays;
            streak = 0;
            result.currentCompleteStreak = 0;
        }
        // Future days that are still empty break nothing: they are simply not
        // due yet, so the streak is left as it stands.
    }

    return result;
}

QVector<Statistics::BreakdownRow> Statistics::breakdownBy(const QVector<WorkEntry> &entries,
                                                          WorkEntry::Field field)
{
    if (!groupableFields().contains(field))
        return QVector<BreakdownRow>();

    QMap<QString, BreakdownRow> rows;
    int total = 0;

    for (const WorkEntry &entry : entries) {
        const QString key = groupKeyFor(entry, field);
        BreakdownRow &row = rows[key];
        row.key = key;
        row.minutes += entry.minutes;
        if (entry.billable)
            row.billableMinutes += entry.minutes;
        ++row.entryCount;
        total += entry.minutes;
    }

    QVector<BreakdownRow> result;
    result.reserve(rows.size());
    QMap<QString, BreakdownRow>::const_iterator it = rows.constBegin();
    for (; it != rows.constEnd(); ++it) {
        BreakdownRow row = it.value();
        row.share = total > 0 ? static_cast<double>(row.minutes) / static_cast<double>(total) : 0.0;
        result.append(row);
    }

    std::sort(result.begin(), result.end(), breakdownRowIsBefore);
    return result;
}

QVector<WorkEntry::Field> Statistics::groupableFields()
{
    return QVector<WorkEntry::Field>{
        WorkEntry::FieldProject,
        WorkEntry::FieldIssueKey,
        WorkEntry::FieldActivity,
        WorkEntry::FieldSprint,
        WorkEntry::FieldFixVersion,
        WorkEntry::FieldEpic,
        WorkEntry::FieldComponent,
        WorkEntry::FieldIssueType,
        WorkEntry::FieldStatus,
        WorkEntry::FieldLocation,
        WorkEntry::FieldTags,
        WorkEntry::FieldWeekday
    };
}
