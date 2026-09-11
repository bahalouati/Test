#include "export/TextReport.h"

#include "data/Statistics.h"

#include <QLocale>
#include <QStringList>

namespace {

/*! Escapes the characters that would otherwise be read as markup. */
QString escapeHtml(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1String("&"), QLatin1String("&amp;"));
    escaped.replace(QLatin1String("<"), QLatin1String("&lt;"));
    escaped.replace(QLatin1String(">"), QLatin1String("&gt;"));
    return escaped;
}

/*! One "label: value" row of the detail panel, skipped when the value is empty. */
QString detailRow(const QString &label, const QString &value)
{
    if (value.trimmed().isEmpty())
        return QString();
    return QStringLiteral(
               "<tr><td style='padding:2px 10px 2px 0; color:#666; white-space:nowrap;'>%1</td>"
               "<td style='padding:2px 0;'>%2</td></tr>")
        .arg(escapeHtml(label), value);
}

/*! The same, with the value turned into a clickable link. */
QString linkRow(const QString &label, const QString &url, const QString &text = QString())
{
    if (url.trimmed().isEmpty())
        return QString();
    const QString caption = text.trimmed().isEmpty() ? url : text;
    return detailRow(label, QStringLiteral("<a href=\"%1\">%2</a>")
                                .arg(escapeHtml(url), escapeHtml(caption)));
}

} // namespace

QString TextReport::standupNote(const DaySummary &day, const QVector<WorkEntry> &entries)
{
    QStringList lines;
    lines << QStringLiteral("%1 - %2")
                 .arg(QLocale().toString(day.date, QLocale::LongFormat),
                      Duration::format(day.totalMinutes));

    if (day.meta.type != DayType::Workday)
        lines << QStringLiteral("(%1)").arg(dayTypeDisplayName(day.meta.type));
    if (!day.meta.note.trimmed().isEmpty())
        lines << day.meta.note.trimmed();

    if (entries.isEmpty()) {
        lines << QStringLiteral("- nothing logged");
        return lines.join(QLatin1Char('\n'));
    }

    for (const WorkEntry &entry : entries) {
        QString line = QStringLiteral("- %1 (%2)")
                           .arg(entry.title(), Duration::format(entry.minutes));

        QStringList context;
        if (!entry.sprint.trimmed().isEmpty())
            context << entry.sprint.trimmed();
        if (entry.status != EntryStatus::InProgress)
            context << entryStatusDisplayName(entry.status);
        if (!context.isEmpty())
            line += QStringLiteral(" [%1]").arg(context.join(QStringLiteral(", ")));

        lines << line;

        const QString notes = entry.description.trimmed();
        if (!notes.isEmpty()) {
            const QStringList noteLines = notes.split(QLatin1Char('\n'));
            for (const QString &noteLine : noteLines)
                lines << QStringLiteral("    %1").arg(noteLine.trimmed());
        }
    }

    return lines.join(QLatin1Char('\n'));
}

QString TextReport::periodRecap(const QVector<DaySummary> &days,
                                const QVector<WorkEntry> &entries,
                                const Settings &settings)
{
    Q_UNUSED(settings)

    if (days.isEmpty())
        return QStringLiteral("Nothing to report.");

    const Statistics::PeriodTotals totals = Statistics::totals(days, QDate::currentDate());

    QStringList lines;
    lines << QStringLiteral("Work log %1 to %2")
                 .arg(totals.from.toString(Qt::ISODate), totals.to.toString(Qt::ISODate));
    lines << QString();
    lines << QStringLiteral("Logged:   %1").arg(Duration::format(totals.totalMinutes));
    lines << QStringLiteral("Expected: %1").arg(Duration::format(totals.targetMinutes));
    lines << QStringLiteral("Balance:  %1").arg(Duration::formatSigned(totals.balanceMinutes()));
    lines << QStringLiteral("Days:     %1 worked of %2 expected, %3 reached the target")
                 .arg(totals.daysWithWork)
                 .arg(totals.expectedDays)
                 .arg(totals.completeDays);

    const QVector<Statistics::BreakdownRow> byProject =
        Statistics::breakdownBy(entries, WorkEntry::FieldProject);
    if (!byProject.isEmpty()) {
        lines << QString() << QStringLiteral("By project");
        for (const Statistics::BreakdownRow &row : byProject) {
            lines << QStringLiteral("  %1  %2  (%3 %)")
                         .arg(row.key, Duration::format(row.minutes))
                         .arg(row.share * 100.0, 0, 'f', 0);
        }
    }

    const QVector<Statistics::BreakdownRow> byActivity =
        Statistics::breakdownBy(entries, WorkEntry::FieldActivity);
    if (!byActivity.isEmpty()) {
        lines << QString() << QStringLiteral("By activity");
        for (const Statistics::BreakdownRow &row : byActivity) {
            lines << QStringLiteral("  %1  %2  (%3 %)")
                         .arg(row.key, Duration::format(row.minutes))
                         .arg(row.share * 100.0, 0, 'f', 0);
        }
    }

    return lines.join(QLatin1Char('\n'));
}

QString TextReport::entryDetailHtml(const WorkEntry &entry, const Settings &settings)
{
    if (entry.id < 0 && entry.minutes == 0 && entry.issueKey.isEmpty())
        return QStringLiteral("<p style='color:#888;'>Select an entry to see its details.</p>");

    const Duration::Format format = settings.durationFormat();

    QString html;
    html += QStringLiteral("<div style='font-family:sans-serif;'>");

    // Headline: what was worked on, and for how long.
    html += QStringLiteral("<div style='font-size:13pt; font-weight:bold;'>%1</div>")
                .arg(escapeHtml(entry.title()));

    QStringList headlineParts;
    headlineParts << Duration::format(entry.minutes, format);
    headlineParts << QLocale().toString(entry.date, QLocale::LongFormat);
    if (entry.startTime.isValid()) {
        QString window = entry.startTime.toString(QStringLiteral("HH:mm"));
        if (entry.endTime.isValid())
            window += QStringLiteral(" - %1").arg(entry.endTime.toString(QStringLiteral("HH:mm")));
        headlineParts << window;
    }
    html += QStringLiteral("<div style='color:#555; margin-bottom:8px;'>%1</div>")
                .arg(escapeHtml(headlineParts.join(QStringLiteral("  |  "))));

    html += QStringLiteral("<table cellspacing='0' cellpadding='0'>");

    html += detailRow(QStringLiteral("Activity"), escapeHtml(activityDisplayName(entry.activity)));
    html += detailRow(QStringLiteral("Status"), escapeHtml(entryStatusDisplayName(entry.status)));
    if (entry.priority != Priority::Unset)
        html += detailRow(QStringLiteral("Priority"), escapeHtml(priorityDisplayName(entry.priority)));
    html += detailRow(QStringLiteral("Project"), escapeHtml(entry.effectiveProject()));
    html += detailRow(QStringLiteral("Issue type"), escapeHtml(entry.issueType));
    html += detailRow(QStringLiteral("Epic"), escapeHtml(entry.epic));
    html += detailRow(QStringLiteral("Sprint"), escapeHtml(entry.sprint));
    html += detailRow(QStringLiteral("Component"), escapeHtml(entry.component));
    html += detailRow(QStringLiteral("Fix version"), escapeHtml(entry.fixVersion));
    html += detailRow(QStringLiteral("Branch"), escapeHtml(entry.branch));
    html += detailRow(QStringLiteral("Tags"), escapeHtml(entry.tags));
    if (entry.location != WorkLocation::Unset)
        html += detailRow(QStringLiteral("Location"), escapeHtml(workLocationDisplayName(entry.location)));
    html += detailRow(QStringLiteral("Billable"),
                      entry.billable ? QStringLiteral("Yes") : QStringLiteral("No"));

    html += linkRow(QStringLiteral("Issue"), entry.issueUrl,
                    entry.issueKey.isEmpty() ? entry.issueUrl : entry.issueKey);
    html += linkRow(QStringLiteral("Merge request"), entry.mergeRequest);
    html += linkRow(QStringLiteral("Testsheet"), entry.testsheetUrl, entry.testsheetName);
    if (entry.testsheetUrl.trimmed().isEmpty())
        html += detailRow(QStringLiteral("Testsheet"), escapeHtml(entry.testsheetName));

    html += QStringLiteral("</table>");

    if (!entry.description.trimmed().isEmpty()) {
        html += QStringLiteral(
                    "<div style='margin-top:10px; padding:8px; background:#00000010; "
                    "border-radius:4px; white-space:pre-wrap;'>%1</div>")
                    .arg(escapeHtml(entry.description.trimmed()));
    }

    QStringList footnotes;
    footnotes << QStringLiteral("Source: %1").arg(entrySourceDisplayName(entry.source));
    if (!entry.externalId.isEmpty())
        footnotes << QStringLiteral("External id: %1").arg(entry.externalId);
    if (entry.createdAt.isValid()) {
        footnotes << QStringLiteral("Created %1")
                         .arg(QLocale().toString(entry.createdAt, QLocale::ShortFormat));
    }
    if (entry.updatedAt.isValid() && entry.updatedAt != entry.createdAt) {
        footnotes << QStringLiteral("Updated %1")
                         .arg(QLocale().toString(entry.updatedAt, QLocale::ShortFormat));
    }

    html += QStringLiteral("<div style='margin-top:10px; color:#888; font-size:8pt;'>%1</div>")
                .arg(escapeHtml(footnotes.join(QStringLiteral("  |  "))));

    html += QStringLiteral("</div>");
    return html;
}
