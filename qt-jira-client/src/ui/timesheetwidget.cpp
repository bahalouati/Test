#include "timesheetwidget.h"
#include "ui_timesheetwidget.h"

#include "core/jiraclient.h"
#include "core/timesheetexport.h"

#include <QAction>
#include <QComboBox>
#include <QMenu>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

using jira::DayStatus;
using jira::DaySummary;
using jira::TimesheetEntry;

namespace {

// The same bands the Excel report uses, so the two agree at a glance.
QColor fillFor(DayStatus status)
{
    switch (status) {
    case DayStatus::Future:
        return QColor(QStringLiteral("#D9D9D9"));
    case DayStatus::Holiday:
        return QColor(QStringLiteral("#DDEBF7"));
    case DayStatus::Complete:
        return QColor(QStringLiteral("#C6EFCE"));
    case DayStatus::Partial:
        return QColor(QStringLiteral("#FFF2CC"));
    case DayStatus::Short:
        return QColor(QStringLiteral("#FFC7CE"));
    }
    return {};
}

QString formatHours(double hours)
{
    return QLocale().toString(hours, 'f', hours == qRound(hours) ? 0 : 2);
}

// The work-log table's columns. Kept in step with the <column> list in
// timesheetwidget.ui -- reorder both together.
enum Column {
    DateColumn,
    IssueColumn,
    SummaryColumn,
    HoursColumn,
    SprintColumn,
    FixVersionColumn,
    MergeRequestColumn,
    TestSheetColumn,
    SpecificationsColumn,
    DescriptionColumn
};

} // namespace

TimesheetWidget::TimesheetWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TimesheetWidget)
    , m_settings(jira::TimesheetSettings::load())
{
    ui->setupUi(this);

    const QDate today = QDate::currentDate();

    // Populated here rather than in the .ui: the entries are locale month names
    // and a window of years around today, and each carries its value as data.
    for (int month = 1; month <= 12; ++month)
        ui->month->addItem(QLocale().monthName(month), month);
    ui->month->setCurrentIndex(today.month() - 1);

    for (int year = today.year() - 3; year <= today.year() + 1; ++year)
        ui->year->addItem(QString::number(year), year);
    ui->year->setCurrentText(QString::number(today.year()));

    ui->progress->setVisible(false);
    ui->calendar->setContextMenuPolicy(Qt::CustomContextMenu);
    m_holidays.load();
    ui->calendar->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(ui->refresh, &QPushButton::clicked, this, &TimesheetWidget::refresh);
    connect(ui->exportCsv, &QPushButton::clicked, this, &TimesheetWidget::exportCsv);
    connect(ui->exportXlsx, &QPushButton::clicked, this, &TimesheetWidget::exportXlsx);
    connect(ui->month, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TimesheetWidget::monthChanged);
    connect(ui->year, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TimesheetWidget::monthChanged);
    connect(ui->calendar, &QTableWidget::cellDoubleClicked, this, &TimesheetWidget::cellActivated);
    connect(ui->calendar, &QWidget::customContextMenuRequested,
            this, &TimesheetWidget::showCalendarMenu);
    connect(ui->table, &QTableWidget::cellDoubleClicked, this, &TimesheetWidget::cellActivated);

    buildCalendar();
    updateSummary();
}

TimesheetWidget::~TimesheetWidget()
{
    delete ui;
}

void TimesheetWidget::setClient(jira::Client *client)
{
    m_client = client;
    delete m_loader;
    m_loader = new jira::TimesheetLoader(client, this);
    connect(m_loader, &jira::TimesheetLoader::finished, this, &TimesheetWidget::loadFinished);
    connect(m_loader, &jira::TimesheetLoader::failed, this, &TimesheetWidget::loadFailed);
    connect(m_loader, &jira::TimesheetLoader::progress, this, &TimesheetWidget::loadProgress);
}

QDate TimesheetWidget::firstOfMonth() const
{
    return QDate(ui->year->currentData().toInt(), ui->month->currentData().toInt(), 1);
}

QDate TimesheetWidget::lastOfMonth() const
{
    const QDate first = firstOfMonth();
    return QDate(first.year(), first.month(), first.daysInMonth());
}

void TimesheetWidget::setIdentity(const jira::User &me)
{
    m_me = me;
}

void TimesheetWidget::monthChanged()
{
    m_entries.clear();
    buildCalendar();
    buildTable();
    updateSummary();
}

void TimesheetWidget::refresh()
{
    if (!m_client || !m_loader || !m_client->isConfigured()) {
        emit errorOccurred(tr("Connect to a Jira server first."));
        return;
    }
    m_settings = jira::TimesheetSettings::load();
    ui->refresh->setEnabled(false);
    ui->progress->setVisible(true);
    ui->progress->setRange(0, 0);
    m_loader->load(firstOfMonth(), lastOfMonth(), m_me, m_settings);
}

void TimesheetWidget::loadProgress(int done, int total, const QString &message)
{
    ui->progress->setVisible(true);
    if (total > 0) {
        ui->progress->setRange(0, total);
        ui->progress->setValue(done);
    } else {
        ui->progress->setRange(0, 0);
    }
    emit statusMessage(message);
}

void TimesheetWidget::loadFailed(const QString &message)
{
    ui->refresh->setEnabled(true);
    ui->progress->setVisible(false);
    ui->calendar->setContextMenuPolicy(Qt::CustomContextMenu);
    m_holidays.load();
    emit errorOccurred(message);
}

void TimesheetWidget::loadFinished(const QList<TimesheetEntry> &entries)
{
    ui->refresh->setEnabled(true);
    ui->progress->setVisible(false);
    ui->calendar->setContextMenuPolicy(Qt::CustomContextMenu);
    m_holidays.load();
    m_entries = entries;
    ui->exportCsv->setEnabled(!entries.isEmpty());
    ui->exportXlsx->setEnabled(!entries.isEmpty());

    buildCalendar();
    buildTable();
    updateSummary();
    emit statusMessage(tr("Loaded %1 work log entries.").arg(entries.size()));
}

void TimesheetWidget::buildCalendar()
{
    const QDate first = firstOfMonth();
    const QDate last = lastOfMonth();
    m_days = jira::summariseDays(m_entries, first, last, QDate::currentDate(), m_settings.rules,
                                 m_holidays);

    // Lay the month out as weeks of Mon-Fri, the way the Excel calendar reads.
    const int leading = first.dayOfWeek() - 1;           // Monday == 0
    const int weeks = (leading + last.day() + 6) / 7;
    ui->calendar->clearContents();
    ui->calendar->setRowCount(qMax(1, weeks));

    QHash<QDate, DaySummary> byDay;
    for (const DaySummary &day : std::as_const(m_days))
        byDay.insert(day.day, day);

    for (QDate day = first; day <= last; day = day.addDays(1)) {
        const int weekday = day.dayOfWeek();
        if (weekday > Qt::Friday)
            continue;
        const int row = (leading + day.day() - 1) / 7;
        const int column = weekday - 1;

        const DaySummary summary = byDay.value(day);
        QString text = QStringLiteral("%1\n%2")
                               .arg(day.day())
                               .arg(tr("Total: %1h").arg(formatHours(summary.hours)));

        if (summary.isHoliday()) {
            text += QLatin1Char('\n') + tr("Holiday");
        } else if (summary.isMissing(m_settings.rules)) {
            text += QLatin1Char('\n')
                    + tr("Missing %1h").arg(formatHours(summary.missingHours(m_settings.rules)));
        }
        if (!summary.entries.isEmpty()) {
            text += QStringLiteral("\n\n");
            QStringList lines;
            for (const TimesheetEntry &entry : summary.entries)
                lines.append(entry.calendarLine());
            text += lines.join(QLatin1Char('\n'));
        }

        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
        item->setBackground(fillFor(summary.status));
        item->setForeground(QColor(QStringLiteral("#1a1a1a")));
        item->setData(Qt::UserRole, day);
        item->setToolTip(jira::dayStatusLabel(summary.status));
        ui->calendar->setItem(row, column, item);
    }

    ui->calendar->resizeRowsToContents();
}

void TimesheetWidget::buildTable()
{
    ui->table->setSortingEnabled(false);
    ui->table->setRowCount(m_entries.size());

    QList<TimesheetEntry> sorted = m_entries;
    std::sort(sorted.begin(), sorted.end(), [](const TimesheetEntry &a, const TimesheetEntry &b) {
        if (a.day != b.day)
            return a.day < b.day;
        return a.issueKey < b.issueKey;
    });

    // Built from its code point rather than typed into the source, so it cannot
    // be mangled by a compiler that reads this file as anything but UTF-8.
    const QString dash(QChar(0x2014));

    for (int row = 0; row < sorted.size(); ++row) {
        const TimesheetEntry &entry = sorted.at(row);
        const auto set = [this, row](int column, const QString &text) {
            auto *item = new QTableWidgetItem(text);
            ui->table->setItem(row, column, item);
            return item;
        };

        set(DateColumn, entry.day.toString(Qt::ISODate));
        set(IssueColumn, entry.issueKey)->setData(Qt::UserRole, entry.issueKey);
        set(SummaryColumn, entry.summary);
        set(HoursColumn, formatHours(entry.hours));
        set(SprintColumn, entry.sprint.isEmpty() ? dash : entry.sprint);
        set(FixVersionColumn, entry.fixVersions.isEmpty() ? dash : entry.fixVersions);

        // The host, not a bare "open": a remote link is set by whoever can edit
        // the issue, so the destination has to be visible without hovering.
        const QString mrHost = QUrl(entry.mergeRequestUrl).host();
        QTableWidgetItem *mr = set(MergeRequestColumn,
                                   entry.hasMergeRequest()
                                           ? (mrHost.isEmpty() ? tr("open") : mrHost)
                                           : dash);
        if (entry.hasMergeRequest()) {
            mr->setData(Qt::UserRole, entry.mergeRequestUrl);
            mr->setForeground(QColor(QStringLiteral("#0b66c3")));
            mr->setToolTip(entry.mergeRequestUrl);
        }

        QTableWidgetItem *sheet = set(TestSheetColumn,
                                      entry.hasTestSheet() ? entry.testSheetName : dash);
        if (entry.hasTestSheet()) {
            sheet->setData(Qt::UserRole, entry.testSheetUrl);
            sheet->setForeground(QColor(QStringLiteral("#0b66c3")));
            sheet->setToolTip(entry.testSheetUrl);
        }

        set(SpecificationsColumn, entry.hasSpecifications() ? entry.specifications : dash);
        set(DescriptionColumn, entry.comment);
    }

    ui->table->setSortingEnabled(true);
    ui->table->resizeColumnsToContents();
    ui->table->horizontalHeader()->setSectionResizeMode(SummaryColumn, QHeaderView::Stretch);
}

void TimesheetWidget::updateSummary()
{
    const double logged = jira::totalLoggedHours(m_days);
    const double missing = jira::totalMissingHours(m_days, m_settings.rules);

    int shortDays = 0;
    for (const DaySummary &day : std::as_const(m_days)) {
        if (day.isMissing(m_settings.rules))
            ++shortDays;
    }

    int holidays = 0;
    for (const DaySummary &day : std::as_const(m_days)) {
        if (day.isHoliday())
            ++holidays;
    }

    QString text = tr("<b>%1 h logged</b> across %2 working days")
                           .arg(formatHours(logged))
                           .arg(m_days.size() - holidays);
    if (holidays > 0) {
        text += QLatin1String(" &nbsp;·&nbsp; ")
                + (holidays == 1 ? tr("1 holiday") : tr("%1 holidays").arg(holidays));
    }
    if (missing > 0.0) {
        text += tr(" &nbsp;·&nbsp; <span style='color:#b3261e;'><b>%1 h missing</b> over %2 %3</span>")
                        .arg(formatHours(missing))
                        .arg(shortDays)
                        .arg(shortDays == 1 ? tr("day") : tr("days"));
    } else if (!m_days.isEmpty()) {
        text += tr(" &nbsp;·&nbsp; <span style='color:#1e7d32;'><b>nothing missing</b></span>");
    }
    text += tr("<br/><span style='color:gray;'>A full day is %1 h; below %2 h counts as short.</span>")
                    .arg(formatHours(m_settings.rules.fullDayHours))
                    .arg(formatHours(m_settings.rules.partialDayHours));
    ui->summary->setText(text);
}

QDate TimesheetWidget::selectedDay() const
{
    QTableWidgetItem *item = ui->calendar->currentItem();
    return item ? item->data(Qt::UserRole).toDate() : QDate();
}

void TimesheetWidget::showCalendarMenu(const QPoint &position)
{
    QTableWidgetItem *item = ui->calendar->itemAt(position);
    if (!item)
        return;
    ui->calendar->setCurrentItem(item);

    const QDate day = item->data(Qt::UserRole).toDate();
    if (!day.isValid())
        return;

    QMenu menu(this);
    QAction *toggle = menu.addAction(m_holidays.contains(day)
                                             ? tr("Not a holiday")
                                             : tr("Mark %1 as a holiday")
                                                       .arg(QLocale().toString(day, QLocale::ShortFormat)));
    connect(toggle, &QAction::triggered, this, &TimesheetWidget::toggleHoliday);
    menu.exec(ui->calendar->viewport()->mapToGlobal(position));
}

void TimesheetWidget::toggleHoliday()
{
    const QDate day = selectedDay();
    if (!day.isValid())
        return;

    const bool nowHoliday = m_holidays.toggle(day);
    m_holidays.save();

    // Re-band the month: the day itself changes colour, and the totals change
    // with it because a holiday is no longer owed.
    buildCalendar();
    updateSummary();

    emit statusMessage(nowHoliday
                               ? tr("%1 is a holiday — it no longer counts as missing.")
                                         .arg(QLocale().toString(day, QLocale::ShortFormat))
                               : tr("%1 is a working day again.")
                                         .arg(QLocale().toString(day, QLocale::ShortFormat)));
}

void TimesheetWidget::cellActivated(int row, int column)
{
    auto *table = qobject_cast<QTableWidget *>(sender());
    if (!table)
        return;
    QTableWidgetItem *item = table->item(row, column);
    if (!item)
        return;

    const QVariant payload = item->data(Qt::UserRole);
    const QString text = payload.toString();
    if (text.startsWith(QLatin1String("http"))) {
        openLink(QUrl(text));
        return;
    }
    if (table == ui->table && column == IssueColumn && !text.isEmpty())
        emit issueActivated(text);
}

void TimesheetWidget::openLink(const QUrl &url)
{
    if (!url.isValid())
        return;

    // Anything pointing away from the configured Jira is confirmed first, with
    // the address shown: the URL came out of an issue someone else may have
    // edited, and only the host gives that away.
    const QString instance = QUrl(m_client ? m_client->credentials().baseUrl : QString()).host();
    if (!instance.isEmpty() && url.host().compare(instance, Qt::CaseInsensitive) != 0) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("Open a link outside Jira"),
                tr("This link leaves %1 and goes to:\n\n%2\n\nIt came from the issue, so it is "
                   "whatever was put there. Open it?")
                        .arg(instance, url.toString()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    QDesktopServices::openUrl(url);
}

void TimesheetWidget::exportXlsx()
{
    const QDate month = firstOfMonth();
    const QString path = QFileDialog::getSaveFileName(
            this, tr("Export work log"), jira::suggestedWorkbookName(month),
            tr("Excel workbooks (*.xlsx)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!jira::exportTimesheetWorkbook(path, m_entries, m_days, month, m_settings.rules, &error)) {
        emit errorOccurred(tr("Could not write %1: %2").arg(path, error));
        return;
    }
    emit statusMessage(tr("Wrote %1 rows to %2.").arg(m_entries.size()).arg(path));
}

void TimesheetWidget::exportCsv()
{
    const QDate first = firstOfMonth();
    const QString suggested =
            QStringLiteral("Jira_Worklog_%1.csv").arg(first.toString(QStringLiteral("yyyy_MM")));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export work log"), suggested,
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(tr("Could not write %1: %2").arg(path, file.errorString()));
        return;
    }
    file.write(jira::buildTimesheetCsv(m_entries));
    file.close();
    emit statusMessage(tr("Exported %1 rows to %2.").arg(m_entries.size()).arg(path));
}
