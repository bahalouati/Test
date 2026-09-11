#include "timesheetwidget.h"

#include "core/jiraclient.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTextStream>
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

QString csvField(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

} // namespace

TimesheetWidget::TimesheetWidget(jira::Client *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
    , m_loader(new jira::TimesheetLoader(client, this))
    , m_settings(jira::TimesheetSettings::load())
{
    const QDate today = QDate::currentDate();

    m_month = new QComboBox(this);
    for (int month = 1; month <= 12; ++month)
        m_month->addItem(QLocale().monthName(month), month);
    m_month->setCurrentIndex(today.month() - 1);

    m_year = new QComboBox(this);
    for (int year = today.year() - 3; year <= today.year() + 1; ++year)
        m_year->addItem(QString::number(year), year);
    m_year->setCurrentText(QString::number(today.year()));

    m_refresh = new QPushButton(tr("Refresh"), this);
    m_export = new QPushButton(tr("Export CSV…"), this);
    m_export->setEnabled(false);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    m_progress->setMaximumWidth(220);

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Month:"), this));
    controls->addWidget(m_month);
    controls->addWidget(m_year);
    controls->addWidget(m_refresh);
    controls->addSpacing(12);
    controls->addWidget(m_progress);
    controls->addStretch();
    controls->addWidget(m_export);

    m_summary = new QLabel(this);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setWordWrap(true);

    m_legend = new QLabel(this);
    m_legend->setTextFormat(Qt::RichText);
    m_legend->setText(tr("<span style='background:#C6EFCE;'>&nbsp;full day&nbsp;</span> &nbsp; "
                         "<span style='background:#FFF2CC;'>&nbsp;under a full day&nbsp;</span> &nbsp; "
                         "<span style='background:#FFC7CE;'>&nbsp;missing hours&nbsp;</span> &nbsp; "
                         "<span style='background:#D9D9D9;'>&nbsp;upcoming&nbsp;</span>"));

    m_calendar = new QTableWidget(0, 5, this);
    m_calendar->setHorizontalHeaderLabels({tr("Monday"), tr("Tuesday"), tr("Wednesday"),
                                           tr("Thursday"), tr("Friday")});
    m_calendar->verticalHeader()->setVisible(false);
    m_calendar->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_calendar->setSelectionMode(QAbstractItemView::SingleSelection);
    m_calendar->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_calendar->setWordWrap(true);

    m_table = new QTableWidget(0, 9, this);
    m_table->setHorizontalHeaderLabels({tr("Date"), tr("Issue"), tr("Summary"), tr("Fix version"),
                                        tr("Sprint"), tr("Merge request"), tr("Test sheet"),
                                        tr("Hours"), tr("Work description")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addWidget(m_summary);
    layout->addWidget(m_legend);
    layout->addWidget(m_calendar, 3);
    layout->addWidget(m_table, 2);

    connect(m_refresh, &QPushButton::clicked, this, &TimesheetWidget::refresh);
    connect(m_export, &QPushButton::clicked, this, &TimesheetWidget::exportCsv);
    connect(m_month, &QComboBox::currentIndexChanged, this, &TimesheetWidget::monthChanged);
    connect(m_year, &QComboBox::currentIndexChanged, this, &TimesheetWidget::monthChanged);
    connect(m_calendar, &QTableWidget::cellDoubleClicked, this, &TimesheetWidget::cellActivated);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &TimesheetWidget::cellActivated);

    connect(m_loader, &jira::TimesheetLoader::finished, this, &TimesheetWidget::loadFinished);
    connect(m_loader, &jira::TimesheetLoader::failed, this, &TimesheetWidget::loadFailed);
    connect(m_loader, &jira::TimesheetLoader::progress, this, &TimesheetWidget::loadProgress);

    buildCalendar();
    updateSummary();
}

QDate TimesheetWidget::firstOfMonth() const
{
    return QDate(m_year->currentData().toInt(), m_month->currentData().toInt(), 1);
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
    if (!m_client->isConfigured()) {
        emit errorOccurred(tr("Connect to a Jira server first."));
        return;
    }
    m_settings = jira::TimesheetSettings::load();
    m_refresh->setEnabled(false);
    m_progress->setVisible(true);
    m_progress->setRange(0, 0);
    m_loader->load(firstOfMonth(), lastOfMonth(), m_me, m_settings);
}

void TimesheetWidget::loadProgress(int done, int total, const QString &message)
{
    m_progress->setVisible(true);
    if (total > 0) {
        m_progress->setRange(0, total);
        m_progress->setValue(done);
    } else {
        m_progress->setRange(0, 0);
    }
    emit statusMessage(message);
}

void TimesheetWidget::loadFailed(const QString &message)
{
    m_refresh->setEnabled(true);
    m_progress->setVisible(false);
    emit errorOccurred(message);
}

void TimesheetWidget::loadFinished(const QList<TimesheetEntry> &entries)
{
    m_refresh->setEnabled(true);
    m_progress->setVisible(false);
    m_entries = entries;
    m_export->setEnabled(!entries.isEmpty());

    buildCalendar();
    buildTable();
    updateSummary();
    emit statusMessage(tr("Loaded %1 work log entries.").arg(entries.size()));
}

void TimesheetWidget::buildCalendar()
{
    const QDate first = firstOfMonth();
    const QDate last = lastOfMonth();
    m_days = jira::summariseDays(m_entries, first, last, QDate::currentDate(), m_settings.rules);

    // Lay the month out as weeks of Mon-Fri, the way the Excel calendar reads.
    const int leading = first.dayOfWeek() - 1;           // Monday == 0
    const int weeks = (leading + last.day() + 6) / 7;
    m_calendar->clearContents();
    m_calendar->setRowCount(qMax(1, weeks));

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

        if (summary.isMissing(m_settings.rules)) {
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
        m_calendar->setItem(row, column, item);
    }

    m_calendar->resizeRowsToContents();
}

void TimesheetWidget::buildTable()
{
    m_table->setSortingEnabled(false);
    m_table->setRowCount(m_entries.size());

    QList<TimesheetEntry> sorted = m_entries;
    std::sort(sorted.begin(), sorted.end(), [](const TimesheetEntry &a, const TimesheetEntry &b) {
        if (a.day != b.day)
            return a.day < b.day;
        return a.issueKey < b.issueKey;
    });

    const QString dash = QStringLiteral("—");
    for (int row = 0; row < sorted.size(); ++row) {
        const TimesheetEntry &entry = sorted.at(row);
        const auto set = [this, row](int column, const QString &text) {
            auto *item = new QTableWidgetItem(text);
            m_table->setItem(row, column, item);
            return item;
        };

        set(0, entry.day.toString(Qt::ISODate));
        set(1, entry.issueKey)->setData(Qt::UserRole, entry.issueKey);
        set(2, entry.summary);
        set(3, entry.fixVersions.isEmpty() ? dash : entry.fixVersions);
        set(4, entry.sprint.isEmpty() ? dash : entry.sprint);

        QTableWidgetItem *mr = set(5, entry.hasMergeRequest() ? tr("open") : dash);
        if (entry.hasMergeRequest()) {
            mr->setData(Qt::UserRole, entry.mergeRequestUrl);
            mr->setForeground(QColor(QStringLiteral("#0b66c3")));
            mr->setToolTip(entry.mergeRequestUrl);
        }

        QTableWidgetItem *sheet = set(6, entry.hasTestSheet() ? entry.testSheetName : dash);
        if (entry.hasTestSheet()) {
            sheet->setData(Qt::UserRole, entry.testSheetUrl);
            sheet->setForeground(QColor(QStringLiteral("#0b66c3")));
            sheet->setToolTip(entry.testSheetUrl);
        }

        set(7, formatHours(entry.hours));
        set(8, entry.comment);
    }

    m_table->setSortingEnabled(true);
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
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

    QString text = tr("<b>%1 h logged</b> across %2 working days")
                           .arg(formatHours(logged))
                           .arg(m_days.size());
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
    m_summary->setText(text);
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
        QDesktopServices::openUrl(QUrl(text));
        return;
    }
    if (table == m_table && column == 1 && !text.isEmpty())
        emit issueActivated(text);
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
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit errorOccurred(tr("Could not write %1: %2").arg(path, file.errorString()));
        return;
    }

    QTextStream out(&file);
    out << "Date,Issue,Summary,Fix Version,Sprint,MR Link,Hours,Testsheet,Description\n";
    for (const TimesheetEntry &entry : std::as_const(m_entries)) {
        out << csvField(entry.day.toString(Qt::ISODate)) << ','
            << csvField(entry.issueKey) << ','
            << csvField(entry.summary) << ','
            << csvField(entry.fixVersions) << ','
            << csvField(entry.sprint) << ','
            << csvField(entry.mergeRequestUrl) << ','
            << csvField(formatHours(entry.hours)) << ','
            << csvField(entry.testSheetUrl.isEmpty() ? QString() : entry.testSheetName) << ','
            << csvField(entry.comment) << '\n';
    }
    file.close();
    emit statusMessage(tr("Exported %1 rows to %2.").arg(m_entries.size()).arg(path));
}
