#include "export/ExcelReport.h"

#include "data/Statistics.h"
#include "export/XlsxWriter.h"

#include <QLocale>
#include <QStringList>

namespace {

/*! The palette of the workbook, kept together so it reads as a theme. */
const char *kHeaderFill = "1F4E78";
const char *kHeaderFont = "FFFFFF";
const char *kTotalFill  = "DDEBF7";

/*! The formats used across the sheets, registered once per workbook. */
struct ReportFormats
{
    int header = -1;
    int text = -1;
    int number = -1;
    int link = -1;
    int total = -1;
    int totalNumber = -1;
    int sectionTitle = -1;
    int title = -1;
    int calendarHeader = -1;
    int percent = -1;
    QVector<int> dayStates; //!< one per DaySummary::State, in that order
};

ReportFormats registerFormats(XlsxWriter &workbook, const Settings &settings)
{
    ReportFormats formats;

    XlsxFormat header;
    header.bold = true;
    header.fillColor = QString::fromLatin1(kHeaderFill);
    header.fontColor = QString::fromLatin1(kHeaderFont);
    header.border = true;
    header.verticalAlignment = QStringLiteral("center");
    formats.header = workbook.addFormat(header);

    XlsxFormat text;
    text.border = true;
    text.verticalAlignment = QStringLiteral("top");
    formats.text = workbook.addFormat(text);

    XlsxFormat number = text;
    number.numberFormat = QStringLiteral("0.00");
    number.horizontalAlignment = QStringLiteral("right");
    formats.number = workbook.addFormat(number);

    XlsxFormat percent = text;
    percent.numberFormat = QStringLiteral("0.0%");
    percent.horizontalAlignment = QStringLiteral("right");
    formats.percent = workbook.addFormat(percent);

    XlsxFormat link = text;
    link.fontColor = QStringLiteral("0563C1");
    formats.link = workbook.addFormat(link);

    XlsxFormat total;
    total.bold = true;
    total.border = true;
    total.fillColor = QString::fromLatin1(kTotalFill);
    formats.total = workbook.addFormat(total);

    XlsxFormat totalNumber = total;
    totalNumber.numberFormat = QStringLiteral("0.00");
    totalNumber.horizontalAlignment = QStringLiteral("right");
    formats.totalNumber = workbook.addFormat(totalNumber);

    XlsxFormat sectionTitle;
    sectionTitle.bold = true;
    sectionTitle.fontSize = 12;
    formats.sectionTitle = workbook.addFormat(sectionTitle);

    XlsxFormat reportTitle;
    reportTitle.bold = true;
    reportTitle.fontSize = 16;
    formats.title = workbook.addFormat(reportTitle);

    XlsxFormat calendarHeader = header;
    calendarHeader.horizontalAlignment = QStringLiteral("center");
    formats.calendarHeader = workbook.addFormat(calendarHeader);

    // One wrapped, bordered, coloured format per day state, so the calendar
    // sheet carries the same colour code as the application window.
    for (int state = DaySummary::Complete; state <= DaySummary::NonWorking; ++state) {
        XlsxFormat dayFormat;
        dayFormat.wrapText = true;
        dayFormat.border = true;
        dayFormat.verticalAlignment = QStringLiteral("top");
        dayFormat.fillColor = settings.dayColor(state).name().mid(1).toUpper();
        formats.dayStates.append(workbook.addFormat(dayFormat));
    }

    return formats;
}

/*! True when the column holds a URL that should be written as a hyperlink. */
bool fieldIsLink(WorkEntry::Field field)
{
    return field == WorkEntry::FieldMergeRequest
           || field == WorkEntry::FieldTestsheetUrl
           || field == WorkEntry::FieldIssueUrl;
}

void writeWorklogSheet(XlsxWriter &workbook,
                       const ReportFormats &formats,
                       const QVector<WorkEntry> &entries,
                       const QVector<WorkEntry::Field> &columns)
{
    XlsxSheet *sheet = workbook.addSheet(QStringLiteral("Worklogs"));

    for (int column = 0; column < columns.size(); ++column) {
        const WorkEntry::Field field = columns.at(column);
        sheet->writeString(1, column + 1, WorkEntry::fieldHeader(field), formats.header);
        sheet->setColumnWidth(column + 1, WorkEntry::fieldWidthHint(field));
    }
    sheet->freezePanes(1, 0);
    sheet->setAutoFilter(1, 1, qMax(1, entries.size() + 1), qMax(1, columns.size()));

    int row = 2;
    for (const WorkEntry &entry : entries) {
        for (int column = 0; column < columns.size(); ++column) {
            const WorkEntry::Field field = columns.at(column);
            const int excelColumn = column + 1;

            if (field == WorkEntry::FieldHours) {
                sheet->writeNumber(row, excelColumn, Duration::toHours(entry.minutes),
                                   formats.number);
                continue;
            }

            if (field == WorkEntry::FieldDate) {
                // ISO text rather than an Excel serial date: it sorts and
                // filters correctly, and it survives any locale.
                sheet->writeString(row, excelColumn, entry.date.toString(Qt::ISODate),
                                   formats.text);
                continue;
            }

            const QString value = entry.field(field).toString();

            if (fieldIsLink(field) && !value.isEmpty()) {
                // The testsheet column shows the file name but links to the file.
                QString label = value;
                if (field == WorkEntry::FieldTestsheetUrl && !entry.testsheetName.isEmpty())
                    label = entry.testsheetName;
                sheet->writeHyperlink(row, excelColumn, label, value, formats.link);
                continue;
            }

            if (field == WorkEntry::FieldTestsheet && !entry.testsheetUrl.isEmpty()) {
                sheet->writeHyperlink(row, excelColumn, value, entry.testsheetUrl, formats.link);
                continue;
            }

            sheet->writeString(row, excelColumn, value, formats.text);
        }
        ++row;
    }

    // A totals line, so the workbook answers "how much did this month cost?"
    // without anyone having to select a column.
    int totalMinutes = 0;
    int billableMinutes = 0;
    for (const WorkEntry &entry : entries) {
        totalMinutes += entry.minutes;
        if (entry.billable)
            billableMinutes += entry.minutes;
    }

    for (int column = 0; column < columns.size(); ++column) {
        const WorkEntry::Field field = columns.at(column);
        if (field == WorkEntry::FieldHours) {
            sheet->writeNumber(row, column + 1, Duration::toHours(totalMinutes),
                               formats.totalNumber);
        } else if (field == WorkEntry::FieldDuration) {
            sheet->writeString(row, column + 1, Duration::format(totalMinutes), formats.total);
        } else if (column == 0) {
            sheet->writeString(row, column + 1,
                               QStringLiteral("Total (%1 entries)").arg(entries.size()),
                               formats.total);
        } else if (field == WorkEntry::FieldBillable) {
            sheet->writeString(row, column + 1, Duration::format(billableMinutes), formats.total);
        } else {
            sheet->writeString(row, column + 1, QString(), formats.total);
        }
    }
}

/*! The text of one calendar cell: the day, its total, then its entries. */
QString calendarCellText(const DaySummary &day, const Settings &settings)
{
    QStringList lines;
    lines << QString::number(day.date.day());

    QString totals = QStringLiteral("Total: %1").arg(Duration::format(day.totalMinutes));
    if (day.targetMinutes > 0) {
        totals += QStringLiteral(" / %1 (%2)")
                      .arg(Duration::format(day.targetMinutes),
                           Duration::formatSigned(day.balanceMinutes()));
    }
    lines << totals;

    if (day.meta.type != DayType::Workday)
        lines << dayTypeDisplayName(day.meta.type);
    if (!day.meta.note.trimmed().isEmpty())
        lines << day.meta.note.trimmed();

    if (!day.lines.isEmpty()) {
        lines << QString();
        for (const DayLine &line : day.lines) {
            QString text = QStringLiteral("%1 (%2)")
                               .arg(line.label, Duration::format(line.minutes));
            if (!line.detail.isEmpty())
                text += QStringLiteral("\n   %1").arg(line.detail);
            lines << text;
        }
    }

    Q_UNUSED(settings)
    return lines.join(QLatin1Char('\n'));
}

void writeCalendarSheet(XlsxWriter &workbook,
                        const ReportFormats &formats,
                        const QVector<DaySummary> &days,
                        const Settings &settings)
{
    if (days.isEmpty())
        return;

    XlsxSheet *sheet = workbook.addSheet(QStringLiteral("Calendar"));

    const bool includeWeekend = settings.showWeekends();
    const int lastWeekday = includeWeekend ? Qt::Sunday : Qt::Friday;
    const int columnCount = lastWeekday - Qt::Monday + 1;

    const QLocale locale;
    for (int weekday = Qt::Monday; weekday <= lastWeekday; ++weekday) {
        const int column = weekday - Qt::Monday + 1;
        sheet->writeString(1, column, locale.dayName(weekday), formats.calendarHeader);
        sheet->setColumnWidth(column, 34);
    }
    sheet->freezePanes(1, 0);

    const QDate today = QDate::currentDate();
    const int threshold = settings.warningThresholdMinutes();

    // Each calendar week becomes one spreadsheet row; the first row is the header.
    const QDate firstDay = days.first().date;
    const QDate firstMonday = firstDay.addDays(-(firstDay.dayOfWeek() - Qt::Monday));

    for (const DaySummary &day : days) {
        if (day.date.dayOfWeek() > lastWeekday)
            continue; // a hidden weekend column

        const int weekIndex = static_cast<int>(firstMonday.daysTo(day.date) / 7);
        const int row = weekIndex + 2;
        const int column = day.date.dayOfWeek() - Qt::Monday + 1;

        const int state = static_cast<int>(day.state(today, threshold));
        const int format = formats.dayStates.value(state, formats.text);

        sheet->writeString(row, column, calendarCellText(day, settings), format);
        sheet->setRowHeight(row, 150);
    }

    Q_UNUSED(columnCount)
}

/*! Writes one breakdown block and returns the row after it. */
int writeBreakdownBlock(XlsxSheet *sheet,
                        const ReportFormats &formats,
                        int startRow,
                        const QString &title,
                        const QVector<Statistics::BreakdownRow> &rows)
{
    int row = startRow;
    sheet->writeString(row, 1, title, formats.sectionTitle);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Name"), formats.header);
    sheet->writeString(row, 2, QStringLiteral("Hours"), formats.header);
    sheet->writeString(row, 3, QStringLiteral("Share"), formats.header);
    sheet->writeString(row, 4, QStringLiteral("Entries"), formats.header);
    ++row;

    for (const Statistics::BreakdownRow &breakdown : rows) {
        sheet->writeString(row, 1, breakdown.key, formats.text);
        sheet->writeNumber(row, 2, Duration::toHours(breakdown.minutes), formats.number);
        sheet->writeNumber(row, 3, breakdown.share, formats.percent);
        sheet->writeNumber(row, 4, breakdown.entryCount, formats.text);
        ++row;
    }

    return row + 1; // one blank line before the next block
}

void writeSummarySheet(XlsxWriter &workbook,
                       const ReportFormats &formats,
                       const QVector<WorkEntry> &entries,
                       const QVector<DaySummary> &days,
                       const Settings &settings,
                       const QString &title)
{
    XlsxSheet *sheet = workbook.addSheet(QStringLiteral("Summary"));
    sheet->setColumnWidth(1, 42);
    sheet->setColumnWidth(2, 14);
    sheet->setColumnWidth(3, 12);
    sheet->setColumnWidth(4, 12);

    sheet->writeString(1, 1, title, formats.title);

    const Statistics::PeriodTotals totals = Statistics::totals(days, QDate::currentDate());

    int row = 3;
    sheet->writeString(row, 1, QStringLiteral("Period"), formats.text);
    sheet->writeString(row, 2, QStringLiteral("%1 to %2")
                                   .arg(totals.from.toString(Qt::ISODate),
                                        totals.to.toString(Qt::ISODate)),
                       formats.text);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Logged hours"), formats.text);
    sheet->writeNumber(row, 2, Duration::toHours(totals.totalMinutes), formats.number);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Expected hours"), formats.text);
    sheet->writeNumber(row, 2, Duration::toHours(totals.targetMinutes), formats.number);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Balance"), formats.text);
    sheet->writeNumber(row, 2, Duration::toHours(totals.balanceMinutes()), formats.number);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Billable hours"), formats.text);
    sheet->writeNumber(row, 2, Duration::toHours(totals.billableMinutes), formats.number);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Days with work / expected days"), formats.text);
    sheet->writeString(row, 2, QStringLiteral("%1 / %2")
                                   .arg(totals.daysWithWork)
                                   .arg(totals.expectedDays),
                       formats.text);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Days that reached the target"), formats.text);
    sheet->writeNumber(row, 2, totals.completeDays, formats.text);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Absence days"), formats.text);
    sheet->writeNumber(row, 2, totals.absenceDays, formats.text);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Longest streak at target"), formats.text);
    sheet->writeNumber(row, 2, totals.longestCompleteStreak, formats.text);
    ++row;

    sheet->writeString(row, 1, QStringLiteral("Average per day worked"), formats.text);
    sheet->writeString(row, 2, Duration::format(totals.averageMinutesPerWorkedDay()), formats.text);
    row += 2;

    row = writeBreakdownBlock(sheet, formats, row, QStringLiteral("By project"),
                              Statistics::breakdownBy(entries, WorkEntry::FieldProject));
    row = writeBreakdownBlock(sheet, formats, row, QStringLiteral("By activity"),
                              Statistics::breakdownBy(entries, WorkEntry::FieldActivity));
    row = writeBreakdownBlock(sheet, formats, row, QStringLiteral("By sprint"),
                              Statistics::breakdownBy(entries, WorkEntry::FieldSprint));
    row = writeBreakdownBlock(sheet, formats, row, QStringLiteral("By fix version"),
                              Statistics::breakdownBy(entries, WorkEntry::FieldFixVersion));
    writeBreakdownBlock(sheet, formats, row, QStringLiteral("By issue"),
                        Statistics::breakdownBy(entries, WorkEntry::FieldIssueKey));

    Q_UNUSED(settings)
}

} // namespace

QString ExcelReport::suggestedFileName(const QDate &from, const QDate &to)
{
    if (from.isValid() && to.isValid() && from.year() == to.year() && from.month() == to.month())
        return QStringLiteral("Worklog_%1.xlsx").arg(from.toString(QStringLiteral("yyyy_MM")));

    return QStringLiteral("Worklog_%1_%2.xlsx")
        .arg(from.toString(QStringLiteral("yyyyMMdd")), to.toString(QStringLiteral("yyyyMMdd")));
}

bool ExcelReport::write(const QString &path,
                        const QVector<WorkEntry> &entries,
                        const QVector<DaySummary> &days,
                        const Settings &settings,
                        const Options &options,
                        QString *error)
{
    XlsxWriter workbook;
    const ReportFormats formats = registerFormats(workbook, settings);

    const QVector<WorkEntry::Field> columns =
        options.columns.isEmpty() ? WorkEntry::defaultVisibleFields() : options.columns;

    QString title = options.title;
    if (title.isEmpty() && !days.isEmpty()) {
        title = QStringLiteral("Work log %1 - %2")
                    .arg(days.first().date.toString(Qt::ISODate),
                         days.last().date.toString(Qt::ISODate));
    }

    if (options.includeWorklogSheet)
        writeWorklogSheet(workbook, formats, entries, columns);
    if (options.includeCalendarSheet)
        writeCalendarSheet(workbook, formats, days, settings);
    if (options.includeSummarySheet)
        writeSummarySheet(workbook, formats, entries, days, settings, title);

    return workbook.save(path, error);
}
