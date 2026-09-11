#include "timesheetexport.h"

#include "xlsxwriter.h"

#include <QHash>
#include <QLocale>
#include <QObject>

#include <algorithm>
#include <utility>

namespace jira {

namespace {

xlsx::Style styleFor(DayStatus status)
{
    switch (status) {
    case DayStatus::Future:
        return xlsx::Style::DayFuture;
    case DayStatus::Holiday:
        return xlsx::Style::DayHoliday;
    case DayStatus::Complete:
        return xlsx::Style::DayComplete;
    case DayStatus::Partial:
        return xlsx::Style::DayPartial;
    case DayStatus::Short:
        return xlsx::Style::DayShort;
    }
    return xlsx::Style::DayPlain;
}

QString oneDecimal(double hours)
{
    return QString::number(hours, 'f', 1);
}

} // namespace

QString suggestedWorkbookName(const QDate &month)
{
    return QStringLiteral("Jira_Worklog_Calendar_%1.xlsx")
            .arg(month.toString(QStringLiteral("yyyy_MM")));
}

bool exportTimesheetWorkbook(const QString &path,
                             const QList<TimesheetEntry> &entries,
                             const QList<DaySummary> &days,
                             const QDate &month,
                             const TimesheetRules &rules,
                             QString *errorMessage)
{
    xlsx::Workbook workbook;

    // -----------------------------------------------------------------------
    // Sheet 1: Worklogs
    // -----------------------------------------------------------------------
    xlsx::Sheet *sheet = workbook.addSheet(QStringLiteral("Worklogs"));

    const QStringList headers = {QObject::tr("Date"),        QObject::tr("Issue"),
                                 QObject::tr("Summary"),     QObject::tr("Fix Version"),
                                 QObject::tr("Sprint"),      QObject::tr("MR Link"),
                                 QObject::tr("Hours"),       QObject::tr("Testsheet"),
                                 QObject::tr("Description")};
    for (int column = 0; column < headers.size(); ++column)
        sheet->setText(1, column + 1, headers.at(column), xlsx::Style::Header);

    QList<TimesheetEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const TimesheetEntry &a, const TimesheetEntry &b) {
        if (a.day != b.day)
            return a.day < b.day;
        return a.issueKey < b.issueKey;
    });

    const QString dash = QStringLiteral("-");
    int row = 2;
    // Track the widest text per column so the sheet opens readable, the way the
    // script's auto-width pass does.
    QList<int> widths;
    for (const QString &header : headers)
        widths.append(header.size());

    for (const TimesheetEntry &entry : std::as_const(sorted)) {
        const QString fix = entry.fixVersions.isEmpty() ? dash : entry.fixVersions;
        const QString sprint = entry.sprint.isEmpty() ? dash : entry.sprint;
        const QString sheetName = entry.hasTestSheet() ? entry.testSheetName : dash;
        const QString mrText = entry.hasMergeRequest() ? entry.mergeRequestUrl : dash;

        sheet->setText(row, 1, entry.day.toString(Qt::ISODate));
        sheet->setText(row, 2, entry.issueKey);
        sheet->setText(row, 3, entry.summary);
        sheet->setText(row, 4, fix);
        sheet->setText(row, 5, sprint);

        sheet->setText(row, 6, mrText, entry.hasMergeRequest() ? xlsx::Style::Link : xlsx::Style::Default);
        if (entry.hasMergeRequest())
            sheet->setHyperlink(row, 6, entry.mergeRequestUrl);

        sheet->setNumber(row, 7, entry.hours);

        sheet->setText(row, 8, sheetName, entry.hasTestSheet() ? xlsx::Style::Link : xlsx::Style::Default);
        if (entry.hasTestSheet())
            sheet->setHyperlink(row, 8, entry.testSheetUrl);

        sheet->setText(row, 9, entry.comment);

        const QStringList texts = {entry.day.toString(Qt::ISODate), entry.issueKey, entry.summary,
                                   fix, sprint, mrText, oneDecimal(entry.hours), sheetName,
                                   entry.comment};
        for (int column = 0; column < texts.size(); ++column)
            widths[column] = qMax(widths.at(column), int(texts.at(column).size()));

        ++row;
    }

    for (int column = 0; column < widths.size(); ++column) {
        // The URL columns would otherwise stretch the sheet off the screen.
        const int capped = qMin(widths.at(column) + 3, 60);
        sheet->setColumnWidth(column + 1, capped);
    }

    // -----------------------------------------------------------------------
    // Sheet 2: Calendar
    // -----------------------------------------------------------------------
    xlsx::Sheet *calendar = workbook.addSheet(QStringLiteral("Calendar"));

    const QStringList weekdays = {QObject::tr("Monday"), QObject::tr("Tuesday"),
                                  QObject::tr("Wednesday"), QObject::tr("Thursday"),
                                  QObject::tr("Friday")};
    for (int column = 0; column < weekdays.size(); ++column) {
        calendar->setText(1, column + 1, weekdays.at(column), xlsx::Style::WeekdayHeader);
        calendar->setColumnWidth(column + 1, 32);
    }

    const QDate first(month.year(), month.month(), 1);
    const int leading = first.dayOfWeek() - 1;      // Monday == 0

    QHash<QDate, DaySummary> byDay;
    for (const DaySummary &day : days)
        byDay.insert(day.day, day);

    int lastRow = 1;
    for (int dayNumber = 1; dayNumber <= first.daysInMonth(); ++dayNumber) {
        const QDate day(month.year(), month.month(), dayNumber);
        if (day.dayOfWeek() > Qt::Friday)
            continue;

        const int calendarRow = ((leading + dayNumber - 1) / 7) + 2;
        const int column = day.dayOfWeek();
        lastRow = qMax(lastRow, calendarRow);

        const DaySummary summary = byDay.value(day);
        QString text = QStringLiteral("%1\n%2")
                               .arg(dayNumber)
                               .arg(QObject::tr("Total: %1h").arg(oneDecimal(summary.hours)));
        if (summary.isHoliday()) {
            text += QLatin1Char('\n') + QObject::tr("Holiday");
        } else {
            const double missing = summary.missingHours(rules);
            if (missing > 0.0)
                text += QLatin1Char('\n') + QObject::tr("Missing %1h").arg(oneDecimal(missing));
        }

        if (!summary.entries.isEmpty()) {
            text += QStringLiteral("\n\n");
            QStringList lines;
            for (const TimesheetEntry &entry : summary.entries)
                lines.append(entry.calendarLine());
            text += lines.join(QLatin1Char('\n'));
        }

        // A day outside the summarised range has no status of its own; treat it
        // as plain rather than inventing one.
        const xlsx::Style style = byDay.contains(day) ? styleFor(summary.status) : xlsx::Style::DayPlain;
        calendar->setText(calendarRow, column, text, style);
    }

    for (int calendarRow = 2; calendarRow <= lastRow; ++calendarRow)
        calendar->setRowHeight(calendarRow, 140);

    return workbook.save(path, errorMessage);
}

} // namespace jira
