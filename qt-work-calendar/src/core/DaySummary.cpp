#include "core/DaySummary.h"

#include "core/Duration.h"

int DaySummary::balanceMinutes() const
{
    return totalMinutes - targetMinutes;
}

double DaySummary::completion() const
{
    if (targetMinutes <= 0)
        return totalMinutes > 0 ? 1.0 : 0.0;
    const double ratio = static_cast<double>(totalMinutes) / static_cast<double>(targetMinutes);
    return qBound(0.0, ratio, 1.0);
}

DaySummary::State DaySummary::state(const QDate &today, int warningThresholdMinutes) const
{
    // An absence outranks everything: hours logged on a holiday are still an
    // absence day, they simply appear as overtime in the balance.
    if (!dayTypeExpectsWork(meta.type)) {
        return meta.type == DayType::NonWorking ? NonWorking : Absence;
    }

    // Days with no target (weekend, non-working weekday) stay neutral unless
    // work was actually logged on them, which deserves to be visible.
    if (targetMinutes <= 0 && totalMinutes == 0)
        return NonWorking;

    if (totalMinutes > 0 && targetMinutes > 0 && totalMinutes >= targetMinutes)
        return Complete;
    if (totalMinutes > 0 && targetMinutes <= 0)
        return Complete; // overtime on a free day

    if (date > today)
        return Future;

    if (totalMinutes == 0)
        return Empty;
    if (totalMinutes >= warningThresholdMinutes)
        return Partial;
    return Low;
}

QString DaySummary::shortDescription() const
{
    QString text = Duration::format(totalMinutes);
    if (targetMinutes > 0) {
        text += QStringLiteral(" of %1 (%2)")
                    .arg(Duration::format(targetMinutes),
                         Duration::formatSigned(balanceMinutes()));
    }
    if (entryCount > 0) {
        text += entryCount == 1 ? QStringLiteral(", 1 entry")
                                : QStringLiteral(", %1 entries").arg(entryCount);
    }
    if (meta.type != DayType::Workday)
        text += QStringLiteral(" - %1").arg(dayTypeDisplayName(meta.type));
    return text;
}
