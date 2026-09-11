#include "ui/MainWindow.h"

#include "ui_MainWindow.h"

#include "core/Duration.h"
#include "data/Statistics.h"
#include "export/CsvIo.h"
#include "export/ExcelReport.h"
#include "export/TextReport.h"
#include "ui/AboutDialog.h"
#include "ui/ColumnsDialog.h"
#include "ui/DayMetaDialog.h"
#include "ui/JiraImportDialog.h"
#include "ui/SettingsDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>

namespace {

/*! The periods the report tab offers, in the order of its drop-down. */
enum ReportPeriod {
    PeriodThisMonth,
    PeriodLastMonth,
    PeriodThisWeek,
    PeriodLastWeek,
    PeriodThisYear,
    PeriodCustom
};

/*! How long a status-bar message stays up. */
const int kStatusTimeoutMs = 6000;

/*! No column of the entry table is auto-sized wider than this. */
const int kMaximumColumnWidth = 340;

/*! The columns the compact day list shows; there is no room for more. */
QVector<WorkEntry::Field> dayPanelColumns()
{
    return QVector<WorkEntry::Field>{
        WorkEntry::FieldDuration,
        WorkEntry::FieldIssueKey,
        WorkEntry::FieldSummary,
        WorkEntry::FieldActivity
    };
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
    , m_entryRepository(m_database)
    , m_dayMetaRepository(m_database)
    , m_selectedDate(QDate::currentDate())
{
    m_ui->setupUi(this);

    setupModels();
    setupCalendarTab();
    setupEntriesTab();
    setupReportsTab();
    setupTimerDock();
    setupConnections();
}

MainWindow::~MainWindow()
{
    delete m_ui;
}

// =====================================================================
// Setup
// =====================================================================

void MainWindow::setupModels()
{
    // The visible columns are remembered between sessions as field tokens, so
    // reordering the enum later cannot scramble somebody's layout.
    const QStringList storedTokens = m_settings.visibleEntryFields();
    for (const QString &token : storedTokens) {
        const WorkEntry::Field field = WorkEntry::fieldFromToken(token);
        if (field != WorkEntry::FieldCount)
            m_visibleColumns.append(field);
    }
    if (m_visibleColumns.isEmpty())
        m_visibleColumns = WorkEntry::defaultVisibleFields();

    m_allEntriesModel.setColumns(m_visibleColumns);
    m_entriesProxy.setSourceModel(&m_allEntriesModel);
    m_ui->tableEntries->setModel(&m_entriesProxy);

    m_dayEntriesModel.setColumns(dayPanelColumns());
    m_ui->tableDayEntries->setModel(&m_dayEntriesModel);

    m_ui->tableBreakdown->setModel(&m_breakdownModel);
}

void MainWindow::setupCalendarTab()
{
    m_ui->monthGrid->setSettings(&m_settings);
    m_ui->hoursChart->setSettings(&m_settings);

    m_loadingWidgets = true;
    const QLocale locale;
    for (int month = 1; month <= 12; ++month)
        m_ui->comboMonth->addItem(locale.monthName(month), month);

    const QDate today = QDate::currentDate();
    m_ui->comboMonth->setCurrentIndex(today.month() - 1);
    m_ui->spinYear->setValue(today.year());
    m_loadingWidgets = false;

    // The day list always shows the same four columns, so each one can be told
    // how to behave: the summary takes the room, the rest stay compact.
    QHeaderView *dayHeader = m_ui->tableDayEntries->horizontalHeader();
    dayHeader->setStretchLastSection(false);
    dayHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents); // duration
    dayHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents); // issue
    dayHeader->setSectionResizeMode(2, QHeaderView::Stretch);          // summary
    dayHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents); // activity
    m_ui->tableDayEntries->verticalHeader()->setDefaultSectionSize(
        m_ui->tableDayEntries->fontMetrics().height() + 8);

    // The calendar deserves the room; the day panel needs about a third.
    m_ui->calendarSplitter->setStretchFactor(0, 3);
    m_ui->calendarSplitter->setStretchFactor(1, 2);
}

void MainWindow::setupEntriesTab()
{
    m_loadingWidgets = true;

    m_ui->comboFilterActivity->addItem(QStringLiteral("Any activity"), QString());
    const QVector<Activity> activities = allActivities();
    for (Activity activity : activities) {
        m_ui->comboFilterActivity->addItem(activityDisplayName(activity),
                                           activityDisplayName(activity));
    }

    m_ui->comboFilterBillable->addItem(QStringLiteral("Any"),
                                       static_cast<int>(EntryFilterProxyModel::Any));
    m_ui->comboFilterBillable->addItem(QStringLiteral("Billable"),
                                       static_cast<int>(EntryFilterProxyModel::Yes));
    m_ui->comboFilterBillable->addItem(QStringLiteral("Not billable"),
                                       static_cast<int>(EntryFilterProxyModel::No));

    const QDate today = QDate::currentDate();
    m_ui->dateFilterFrom->setDate(QDate(today.year(), today.month(), 1));
    m_ui->dateFilterTo->setDate(today);
    m_ui->dateFilterFrom->setEnabled(false);
    m_ui->dateFilterTo->setEnabled(false);

    m_loadingWidgets = false;

    // The filter grid alternates label, field, label, field ... Only the field
    // columns are allowed to grow, so a label never drifts away from the
    // control it names.
    for (int column = 0; column < m_ui->filterLayout->columnCount(); ++column)
        m_ui->filterLayout->setColumnStretch(column, column % 2 == 0 ? 0 : 1);

    m_ui->tableEntries->horizontalHeader()->setSectionsMovable(true);
    m_ui->tableEntries->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_ui->tableEntries->horizontalHeader()->setStretchLastSection(false);
    m_ui->tableEntries->verticalHeader()->setDefaultSectionSize(
        m_ui->tableEntries->fontMetrics().height() + 8);
    m_ui->tableEntries->sortByColumn(0, Qt::DescendingOrder);
}

void MainWindow::setupReportsTab()
{
    m_loadingWidgets = true;

    m_ui->comboReportPeriod->addItem(QStringLiteral("This month"), PeriodThisMonth);
    m_ui->comboReportPeriod->addItem(QStringLiteral("Last month"), PeriodLastMonth);
    m_ui->comboReportPeriod->addItem(QStringLiteral("This week"), PeriodThisWeek);
    m_ui->comboReportPeriod->addItem(QStringLiteral("Last week"), PeriodLastWeek);
    m_ui->comboReportPeriod->addItem(QStringLiteral("This year"), PeriodThisYear);
    m_ui->comboReportPeriod->addItem(QStringLiteral("Custom"), PeriodCustom);

    const QVector<WorkEntry::Field> groupable = Statistics::groupableFields();
    for (WorkEntry::Field field : groupable)
        m_ui->comboGroupBy->addItem(WorkEntry::fieldHeader(field), static_cast<int>(field));

    const QDate today = QDate::currentDate();
    m_ui->dateReportFrom->setDate(QDate(today.year(), today.month(), 1));
    m_ui->dateReportTo->setDate(today);
    m_ui->dateReportFrom->setEnabled(false);
    m_ui->dateReportTo->setEnabled(false);

    m_loadingWidgets = false;

    m_ui->tableBreakdown->horizontalHeader()->setStretchLastSection(false);
    m_ui->tableBreakdown->horizontalHeader()->setSectionResizeMode(
        BreakdownTableModel::ColumnKey, QHeaderView::Stretch);
    m_ui->reportSplitter->setStretchFactor(0, 3);
    m_ui->reportSplitter->setStretchFactor(1, 2);
}

void MainWindow::setupTimerDock()
{
    m_loadingWidgets = true;
    const QVector<Activity> activities = allActivities();
    for (Activity activity : activities) {
        m_ui->comboTimerActivity->addItem(activityDisplayName(activity),
                                          activityToString(activity));
    }
    m_ui->comboTimerActivity->setCurrentIndex(
        m_ui->comboTimerActivity->findData(activityToString(m_settings.defaultActivity())));
    m_loadingWidgets = false;

    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, &MainWindow::onTimerTick);
}

void MainWindow::setupConnections()
{
    // ---- calendar navigation
    connect(m_ui->buttonPreviousMonth, &QToolButton::clicked, this, &MainWindow::onPreviousMonth);
    connect(m_ui->buttonNextMonth, &QToolButton::clicked, this, &MainWindow::onNextMonth);
    connect(m_ui->buttonToday, &QPushButton::clicked, this, &MainWindow::onGoToToday);
    connect(m_ui->comboMonth, &QComboBox::currentIndexChanged,
            this, &MainWindow::onMonthComboChanged);
    connect(m_ui->spinYear, &QSpinBox::valueChanged, this, &MainWindow::onYearSpinChanged);

    connect(m_ui->monthGrid, &MonthGridWidget::dateSelected,
            this, &MainWindow::onGridDateSelected);
    connect(m_ui->monthGrid, &MonthGridWidget::dateActivated,
            this, &MainWindow::onGridDateActivated);
    connect(m_ui->monthGrid, &MonthGridWidget::dateContextMenuRequested,
            this, &MainWindow::onGridContextMenuRequested);
    connect(m_ui->hoursChart, &HoursBarChart::dateClicked, this, &MainWindow::onChartDateClicked);

    // ---- day panel
    connect(m_ui->buttonAddEntry, &QPushButton::clicked, this, &MainWindow::onAddEntry);
    connect(m_ui->buttonEditEntry, &QPushButton::clicked, this, &MainWindow::onEditEntry);
    connect(m_ui->buttonDuplicateEntry, &QPushButton::clicked, this, &MainWindow::onDuplicateEntry);
    connect(m_ui->buttonDeleteEntry, &QPushButton::clicked, this, &MainWindow::onDeleteEntry);
    connect(m_ui->buttonDayType, &QPushButton::clicked, this, &MainWindow::onSetDayType);
    connect(m_ui->tableDayEntries, &QTableView::doubleClicked,
            this, &MainWindow::onEntriesDoubleClicked);

    // ---- entries tab
    connect(m_ui->lineSearch, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(m_ui->checkDateRange, &QCheckBox::toggled, this, &MainWindow::onDateRangeToggled);
    connect(m_ui->dateFilterFrom, &QDateEdit::dateChanged, this, &MainWindow::onFilterDateChanged);
    connect(m_ui->dateFilterTo, &QDateEdit::dateChanged, this, &MainWindow::onFilterDateChanged);
    connect(m_ui->comboFilterActivity, &QComboBox::currentIndexChanged,
            this, &MainWindow::onActivityFilterChanged);
    connect(m_ui->comboFilterProject, &QComboBox::currentIndexChanged,
            this, &MainWindow::onProjectFilterChanged);
    connect(m_ui->comboFilterSprint, &QComboBox::currentIndexChanged,
            this, &MainWindow::onSprintFilterChanged);
    connect(m_ui->comboFilterBillable, &QComboBox::currentIndexChanged,
            this, &MainWindow::onBillableFilterChanged);
    connect(m_ui->buttonClearFilters, &QPushButton::clicked, this, &MainWindow::onClearFilters);
    connect(m_ui->buttonChooseColumns, &QPushButton::clicked, this, &MainWindow::onChooseColumns);
    connect(m_ui->tableEntries, &QTableView::doubleClicked,
            this, &MainWindow::onEntriesDoubleClicked);
    connect(m_ui->tableEntries, &QTableView::customContextMenuRequested,
            this, &MainWindow::onEntriesContextMenuRequested);
    connect(m_ui->tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    // ---- reports tab
    connect(m_ui->comboReportPeriod, &QComboBox::currentIndexChanged,
            this, &MainWindow::onReportPeriodChanged);
    connect(m_ui->dateReportFrom, &QDateEdit::dateChanged, this, &MainWindow::onReportDateChanged);
    connect(m_ui->dateReportTo, &QDateEdit::dateChanged, this, &MainWindow::onReportDateChanged);
    connect(m_ui->buttonRefreshReport, &QPushButton::clicked, this, &MainWindow::onRefreshReport);
    connect(m_ui->comboGroupBy, &QComboBox::currentIndexChanged,
            this, &MainWindow::onGroupByChanged);
    connect(m_ui->buttonCopyRecap, &QPushButton::clicked, this, &MainWindow::onCopyRecap);

    // ---- timer dock
    connect(m_ui->buttonTimerStart, &QPushButton::clicked, this, &MainWindow::onTimerStart);
    connect(m_ui->buttonTimerStop, &QPushButton::clicked, this, &MainWindow::onTimerStop);
    connect(m_ui->buttonTimerDiscard, &QPushButton::clicked, this, &MainWindow::onTimerDiscard);

    // ---- actions
    connect(m_ui->actionNewEntry, &QAction::triggered, this, &MainWindow::onAddEntry);
    connect(m_ui->actionEditEntry, &QAction::triggered, this, &MainWindow::onEditEntry);
    connect(m_ui->actionDuplicateEntry, &QAction::triggered, this, &MainWindow::onDuplicateEntry);
    connect(m_ui->actionDeleteEntry, &QAction::triggered, this, &MainWindow::onDeleteEntry);
    connect(m_ui->actionSetDayType, &QAction::triggered, this, &MainWindow::onSetDayType);
    connect(m_ui->actionCopyStandup, &QAction::triggered, this, &MainWindow::onCopyStandup);
    connect(m_ui->actionCopyRecap, &QAction::triggered, this, &MainWindow::onCopyRecap);
    connect(m_ui->actionImportCsv, &QAction::triggered, this, &MainWindow::onImportCsv);
    connect(m_ui->actionImportJira, &QAction::triggered, this, &MainWindow::onImportJira);
    connect(m_ui->actionExportExcel, &QAction::triggered, this, &MainWindow::onExportExcel);
    connect(m_ui->actionExportCsv, &QAction::triggered, this, &MainWindow::onExportCsv);
    connect(m_ui->actionQuit, &QAction::triggered, this, &QWidget::close);
    connect(m_ui->actionPreviousMonth, &QAction::triggered, this, &MainWindow::onPreviousMonth);
    connect(m_ui->actionNextMonth, &QAction::triggered, this, &MainWindow::onNextMonth);
    connect(m_ui->actionToday, &QAction::triggered, this, &MainWindow::onGoToToday);
    connect(m_ui->actionShowWeekends, &QAction::toggled, this, &MainWindow::onShowWeekendsToggled);
    connect(m_ui->actionChooseColumns, &QAction::triggered, this, &MainWindow::onChooseColumns);
    connect(m_ui->actionRefresh, &QAction::triggered, this, &MainWindow::onRefresh);
    connect(m_ui->actionStartTimer, &QAction::triggered, this, &MainWindow::onTimerStart);
    connect(m_ui->actionStopTimer, &QAction::triggered, this, &MainWindow::onTimerStop);
    connect(m_ui->actionDiscardTimer, &QAction::triggered, this, &MainWindow::onTimerDiscard);
    connect(m_ui->actionSettings, &QAction::triggered, this, &MainWindow::onOpenSettings);
    connect(m_ui->actionAbout, &QAction::triggered, this, &MainWindow::onAbout);
    connect(m_ui->actionAboutQt, &QAction::triggered, this, &MainWindow::onAboutQt);

    // Selection models only exist once a model is set, which setupModels() did.
    connect(m_ui->tableDayEntries->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onDayEntrySelectionChanged);
    connect(m_ui->tableEntries->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onEntriesSelectionChanged);
}

bool MainWindow::initialise(const QString &databasePath)
{
    const QString path = databasePath.isEmpty() ? Database::defaultPath() : databasePath;

    QString error;
    if (!m_database.open(path, &error)) {
        QMessageBox::critical(
            this,
            QStringLiteral("Work Calendar"),
            QStringLiteral("The work log database could not be opened.\n\n%1\n\n%2")
                .arg(path, error));
        return false;
    }

    m_loadingWidgets = true;
    m_ui->actionShowWeekends->setChecked(m_settings.showWeekends());
    m_loadingWidgets = false;

    applySettingsToViews();
    restoreLayout();

    reloadMonth();
    reloadAllEntries();
    refreshReports();
    updateTimerDisplay();

    // A remembered header layout wins; otherwise the columns are sized to what
    // is actually in them the first time the application is opened.
    if (m_settings.entryTableHeaderState().isEmpty())
        adjustEntryTableColumns();

    if (timerIsRunning())
        m_tickTimer.start();

    showStatus(QStringLiteral("%1 entries in %2")
                   .arg(m_entryRepository.count())
                   .arg(QDir::toNativeSeparators(path)));
    return true;
}

void MainWindow::applySettingsToViews()
{
    const Duration::Format format = m_settings.durationFormat();
    m_allEntriesModel.setDurationFormat(format);
    m_dayEntriesModel.setDurationFormat(format);
    m_breakdownModel.setDurationFormat(format);
    m_ui->monthGrid->setToday(QDate::currentDate());
}

void MainWindow::adjustEntryTableColumns()
{
    QHeaderView *header = m_ui->tableEntries->horizontalHeader();

    // Start from a clean slate: a column that was stretched for a previous set
    // of columns must not stay stretched for the new one.
    for (int section = 0; section < header->count(); ++section)
        header->setSectionResizeMode(section, QHeaderView::Interactive);

    m_ui->tableEntries->resizeColumnsToContents();

    for (int section = 0; section < header->count(); ++section) {
        if (header->sectionSize(section) > kMaximumColumnWidth)
            header->resizeSection(section, kMaximumColumnWidth);
    }

    const int summarySection = m_visibleColumns.indexOf(WorkEntry::FieldSummary);
    if (summarySection >= 0 && summarySection < header->count())
        header->setSectionResizeMode(summarySection, QHeaderView::Stretch);
    else
        header->setStretchLastSection(true);
}

void MainWindow::restoreLayout()
{
    const QByteArray geometry = m_settings.mainWindowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    const QByteArray state = m_settings.mainWindowState();
    if (!state.isEmpty())
        restoreState(state);

    const QByteArray headerState = m_settings.entryTableHeaderState();
    if (!headerState.isEmpty())
        m_ui->tableEntries->horizontalHeader()->restoreState(headerState);
}

void MainWindow::saveLayout()
{
    m_settings.setMainWindowGeometry(saveGeometry());
    m_settings.setMainWindowState(saveState());
    m_settings.setEntryTableHeaderState(m_ui->tableEntries->horizontalHeader()->saveState());

    QStringList tokens;
    for (WorkEntry::Field field : m_visibleColumns)
        tokens.append(WorkEntry::fieldToken(field));
    m_settings.setVisibleEntryFields(tokens);

    m_settings.sync();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (timerIsRunning()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            QStringLiteral("A timer is still running"),
            QStringLiteral("%1 has not been logged yet.\n\n"
                           "It is kept running and will still be there next time you open the "
                           "application. Close anyway?")
                .arg(Duration::format(timerElapsedSeconds() / 60)),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }

    saveLayout();
    QMainWindow::closeEvent(event);
}

// =====================================================================
// Reloading
// =====================================================================

void MainWindow::reloadMonth()
{
    const QDate from = m_ui->monthGrid->firstVisibleDate();
    const QDate to = m_ui->monthGrid->lastVisibleDate();
    if (!from.isValid() || !to.isValid())
        return;

    m_monthEntries = m_entryRepository.inRange(from, to);
    const QHash<QDate, DayMeta> dayMeta = m_dayMetaRepository.inRange(from, to);
    m_monthDays = Statistics::buildDaySummaries(from, to, m_monthEntries, dayMeta, m_settings);

    m_ui->monthGrid->setDays(m_monthDays);

    // The chart only plots the month itself; the leading and trailing days of
    // the grid would distort the shape of the month.
    QVector<DaySummary> monthOnly;
    for (const DaySummary &day : m_monthDays) {
        if (day.date.month() == m_ui->monthGrid->month()
            && day.date.year() == m_ui->monthGrid->year()) {
            monthOnly.append(day);
        }
    }
    m_ui->hoursChart->setDays(monthOnly);
    m_ui->hoursChart->setSelectedDate(m_selectedDate);

    refreshMonthSummary();
    refreshDayPanel();
}

void MainWindow::refreshMonthSummary()
{
    QVector<DaySummary> monthOnly;
    for (const DaySummary &day : m_monthDays) {
        if (day.date.month() == m_ui->monthGrid->month()
            && day.date.year() == m_ui->monthGrid->year()) {
            monthOnly.append(day);
        }
    }

    const Statistics::PeriodTotals totals = Statistics::totals(monthOnly, QDate::currentDate());
    const Duration::Format format = m_settings.durationFormat();

    const QString balanceColor = totals.balanceMinutes() >= 0 ? QStringLiteral("#1B7F3B")
                                                              : QStringLiteral("#B00020");

    m_ui->labelMonthSummary->setText(
        QStringLiteral("<b>%1</b> of <b>%2</b> &nbsp; "
                       "<span style='color:%3;'><b>%4</b></span> &nbsp;&middot;&nbsp; "
                       "%5 of %6 days at target &nbsp;&middot;&nbsp; %7 entries")
            .arg(Duration::format(totals.totalMinutes, format),
                 Duration::format(totals.targetMinutes, format),
                 balanceColor,
                 Duration::formatSigned(totals.balanceMinutes(), format))
            .arg(totals.completeDays)
            .arg(totals.expectedDays)
            .arg(totals.entryCount));
}

void MainWindow::refreshDayPanel()
{
    m_dayEntries = m_entryRepository.forDate(m_selectedDate);
    m_dayEntriesModel.setEntries(m_dayEntries);

    const DayMeta meta = m_dayMetaRepository.forDate(m_selectedDate);

    DaySummary summary;
    summary.date = m_selectedDate;
    summary.meta = meta;
    summary.targetMinutes = m_settings.targetMinutesFor(m_selectedDate, meta);
    for (const WorkEntry &entry : m_dayEntries) {
        summary.totalMinutes += entry.minutes;
        if (entry.billable)
            summary.billableMinutes += entry.minutes;
        ++summary.entryCount;
    }

    m_ui->labelSelectedDate->setText(QLocale().toString(m_selectedDate, QLocale::LongFormat));

    const Duration::Format format = m_settings.durationFormat();
    QStringList statusParts;
    statusParts << QStringLiteral("%1 logged").arg(Duration::format(summary.totalMinutes, format));
    if (summary.targetMinutes > 0) {
        statusParts << QStringLiteral("target %1").arg(Duration::format(summary.targetMinutes, format));
        statusParts << Duration::formatSigned(summary.balanceMinutes(), format);
    } else {
        statusParts << QStringLiteral("nothing expected");
    }
    if (meta.type != DayType::Workday)
        statusParts << dayTypeDisplayName(meta.type);
    if (summary.billableMinutes != summary.totalMinutes) {
        statusParts << QStringLiteral("%1 billable")
                           .arg(Duration::format(summary.billableMinutes, format));
    }
    if (!meta.note.trimmed().isEmpty())
        statusParts << meta.note.trimmed();

    m_ui->labelDayStatus->setText(statusParts.join(QStringLiteral("  ·  ")));

    m_ui->progressDay->setMaximum(qMax(1, summary.targetMinutes));
    m_ui->progressDay->setValue(qMin(summary.totalMinutes, qMax(1, summary.targetMinutes)));
    m_ui->progressDay->setEnabled(summary.targetMinutes > 0);

    // Selecting the first entry means the detail box is never blank on a day
    // that has something to show.
    if (!m_dayEntries.isEmpty()) {
        m_ui->tableDayEntries->selectRow(0);
    } else {
        m_ui->tableDayEntries->clearSelection();
        updateEntryActions();
        refreshEntryDetail();
    }
}

void MainWindow::updateEntryActions()
{
    bool found = false;
    currentEntry(&found);

    m_ui->actionEditEntry->setEnabled(found);
    m_ui->actionDuplicateEntry->setEnabled(found);
    m_ui->actionDeleteEntry->setEnabled(found);

    m_ui->buttonEditEntry->setEnabled(found);
    m_ui->buttonDuplicateEntry->setEnabled(found);
    m_ui->buttonDeleteEntry->setEnabled(found);
}

void MainWindow::refreshEntryDetail()
{
    bool found = false;
    const WorkEntry entry = currentEntry(&found);
    if (!found) {
        m_ui->textEntryDetail->setHtml(
            QStringLiteral("<p style='color:#888;'>Select an entry to see everything it "
                           "carries: issue, sprint, fix version, links, notes and history.</p>"));
        return;
    }
    m_ui->textEntryDetail->setHtml(TextReport::entryDetailHtml(entry, m_settings));
}

void MainWindow::reloadAllEntries()
{
    m_allEntriesModel.setEntries(m_entryRepository.all());
    m_allEntriesModel.setColumns(m_visibleColumns);
    refreshFilterChoices();
    onEntriesSelectionChanged();

    const int visibleMinutes = m_entriesProxy.visibleMinutes();
    m_ui->labelEntriesSummary->setText(
        QStringLiteral("%1 of %2 entries  ·  %3")
            .arg(m_entriesProxy.rowCount())
            .arg(m_allEntriesModel.rowCount())
            .arg(Duration::format(visibleMinutes, m_settings.durationFormat())));
}

void MainWindow::refreshFilterChoices()
{
    m_loadingWidgets = true;

    const QString previousProject = m_ui->comboFilterProject->currentData().toString();
    m_ui->comboFilterProject->clear();
    m_ui->comboFilterProject->addItem(QStringLiteral("Any project"), QString());
    const QStringList projects = m_entryRepository.distinctValues(WorkEntry::FieldProject);
    for (const QString &project : projects)
        m_ui->comboFilterProject->addItem(project, project);
    // An entry with no explicit project still has one, derived from its key.
    const QStringList issueKeys = m_entryRepository.distinctValues(WorkEntry::FieldIssueKey);
    for (const QString &key : issueKeys) {
        const int dash = key.indexOf(QLatin1Char('-'));
        if (dash <= 0)
            continue;
        const QString derived = key.left(dash).toUpper();
        if (m_ui->comboFilterProject->findData(derived) < 0)
            m_ui->comboFilterProject->addItem(derived, derived);
    }
    const int projectIndex = m_ui->comboFilterProject->findData(previousProject);
    m_ui->comboFilterProject->setCurrentIndex(qMax(0, projectIndex));

    const QString previousSprint = m_ui->comboFilterSprint->currentData().toString();
    m_ui->comboFilterSprint->clear();
    m_ui->comboFilterSprint->addItem(QStringLiteral("Any sprint"), QString());
    const QStringList sprints = m_entryRepository.distinctValues(WorkEntry::FieldSprint);
    for (const QString &sprint : sprints)
        m_ui->comboFilterSprint->addItem(sprint, sprint);
    const int sprintIndex = m_ui->comboFilterSprint->findData(previousSprint);
    m_ui->comboFilterSprint->setCurrentIndex(qMax(0, sprintIndex));

    const QString previousIssue = m_ui->comboTimerIssue->currentText();
    m_ui->comboTimerIssue->clear();
    m_ui->comboTimerIssue->addItem(QString());
    for (const QString &key : issueKeys)
        m_ui->comboTimerIssue->addItem(key);
    m_ui->comboTimerIssue->setCurrentText(previousIssue);

    m_loadingWidgets = false;
}

void MainWindow::reportPeriod(QDate *from, QDate *to) const
{
    const QDate today = QDate::currentDate();
    const int period = m_ui->comboReportPeriod->currentData().toInt();

    switch (period) {
    case PeriodThisMonth:
        *from = QDate(today.year(), today.month(), 1);
        *to = from->addMonths(1).addDays(-1);
        return;
    case PeriodLastMonth:
        *from = QDate(today.year(), today.month(), 1).addMonths(-1);
        *to = from->addMonths(1).addDays(-1);
        return;
    case PeriodThisWeek:
        *from = today.addDays(-(today.dayOfWeek() - Qt::Monday));
        *to = from->addDays(6);
        return;
    case PeriodLastWeek:
        *from = today.addDays(-(today.dayOfWeek() - Qt::Monday) - 7);
        *to = from->addDays(6);
        return;
    case PeriodThisYear:
        *from = QDate(today.year(), 1, 1);
        *to = QDate(today.year(), 12, 31);
        return;
    case PeriodCustom:
    default:
        *from = m_ui->dateReportFrom->date();
        *to = m_ui->dateReportTo->date();
        return;
    }
}

void MainWindow::refreshReports()
{
    QDate from;
    QDate to;
    reportPeriod(&from, &to);
    if (!from.isValid() || !to.isValid() || from > to)
        return;

    m_loadingWidgets = true;
    m_ui->dateReportFrom->setDate(from);
    m_ui->dateReportTo->setDate(to);
    m_loadingWidgets = false;

    const QVector<WorkEntry> entries = m_entryRepository.inRange(from, to);
    const QHash<QDate, DayMeta> dayMeta = m_dayMetaRepository.inRange(from, to);
    const QVector<DaySummary> days =
        Statistics::buildDaySummaries(from, to, entries, dayMeta, m_settings);
    const Statistics::PeriodTotals totals = Statistics::totals(days, QDate::currentDate());

    const Duration::Format format = m_settings.durationFormat();
    m_ui->labelTotalLogged->setText(Duration::format(totals.totalMinutes, format));
    m_ui->labelTotalExpected->setText(Duration::format(totals.targetMinutes, format));
    m_ui->labelTotalBalance->setText(Duration::formatSigned(totals.balanceMinutes(), format));
    m_ui->labelTotalBalance->setStyleSheet(
        totals.balanceMinutes() >= 0 ? QStringLiteral("color: #1B7F3B; font-weight: bold;")
                                     : QStringLiteral("color: #B00020; font-weight: bold;"));
    m_ui->labelTotalBillable->setText(
        QStringLiteral("%1 (%2 %)")
            .arg(Duration::format(totals.billableMinutes, format))
            .arg(totals.billableShare() * 100.0, 0, 'f', 0));
    m_ui->labelTotalDays->setText(QStringLiteral("%1 worked, %2 of %3 at target")
                                      .arg(totals.daysWithWork)
                                      .arg(totals.completeDays)
                                      .arg(totals.expectedDays));
    m_ui->labelTotalStreak->setText(QStringLiteral("%1 now, %2 best")
                                        .arg(totals.currentCompleteStreak)
                                        .arg(totals.longestCompleteStreak));
    m_ui->labelTotalAverage->setText(
        Duration::format(totals.averageMinutesPerWorkedDay(), format));
    m_ui->labelTotalEntries->setText(QString::number(totals.entryCount));
    m_ui->labelTotalAbsences->setText(QString::number(totals.absenceDays));

    const WorkEntry::Field groupField =
        static_cast<WorkEntry::Field>(m_ui->comboGroupBy->currentData().toInt());
    m_breakdownModel.setRows(Statistics::breakdownBy(entries, groupField),
                             WorkEntry::fieldHeader(groupField));

    m_ui->textRecap->setPlainText(TextReport::periodRecap(days, entries, m_settings));
}

// =====================================================================
// Helpers
// =====================================================================

WorkEntry MainWindow::currentEntry(bool *found) const
{
    if (found)
        *found = false;

    // Which table counts depends on the tab in front of the user.
    if (m_ui->tabWidget->currentWidget() == m_ui->tabEntries) {
        const QModelIndexList selected = m_ui->tableEntries->selectionModel()->selectedRows();
        if (selected.isEmpty())
            return WorkEntry();
        const QModelIndex sourceIndex = m_entriesProxy.mapToSource(selected.first());
        if (found)
            *found = true;
        return m_allEntriesModel.entryAt(sourceIndex.row());
    }

    const QModelIndexList selected = m_ui->tableDayEntries->selectionModel()->selectedRows();
    if (selected.isEmpty())
        return WorkEntry();
    if (found)
        *found = true;
    return m_dayEntriesModel.entryAt(selected.first().row());
}

QVector<int> MainWindow::selectedEntryIdsInTable() const
{
    QVector<int> ids;
    const QModelIndexList selected = m_ui->tableEntries->selectionModel()->selectedRows();
    for (const QModelIndex &index : selected) {
        const QModelIndex sourceIndex = m_entriesProxy.mapToSource(index);
        const int id = m_allEntriesModel.entryAt(sourceIndex.row()).id;
        if (id >= 0)
            ids.append(id);
    }
    return ids;
}

EntrySuggestions MainWindow::buildSuggestions() const
{
    EntrySuggestions suggestions;
    suggestions.recentIssues = m_entryRepository.recentIssues(25);
    suggestions.issueTypes = m_entryRepository.distinctValues(WorkEntry::FieldIssueType, 40);
    suggestions.epics = m_entryRepository.distinctValues(WorkEntry::FieldEpic, 40);
    suggestions.sprints = m_entryRepository.distinctValues(WorkEntry::FieldSprint, 40);
    suggestions.components = m_entryRepository.distinctValues(WorkEntry::FieldComponent, 40);
    suggestions.fixVersions = m_entryRepository.distinctValues(WorkEntry::FieldFixVersion, 40);
    suggestions.branches = m_entryRepository.distinctValues(WorkEntry::FieldBranch, 40);

    // "Fill the day" needs to know what is already booked; a year either side
    // of the selected day is far more than the dialog will ever ask about.
    suggestions.loggedMinutesByDate =
        m_entryRepository.minutesByDate(m_selectedDate.addYears(-1), m_selectedDate.addYears(1));

    return suggestions;
}

void MainWindow::showStatus(const QString &message)
{
    m_ui->statusbar->showMessage(message, kStatusTimeoutMs);
}

void MainWindow::showDatabaseError(const QString &whatFailed, const QString &error)
{
    QMessageBox::critical(this, QStringLiteral("Work Calendar"),
                          QStringLiteral("%1\n\n%2").arg(whatFailed, error));
}

bool MainWindow::editEntryInDialog(WorkEntry entry, bool isNew)
{
    EntryDialog dialog(entry, m_settings, buildSuggestions(), this);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    WorkEntry edited = dialog.entry();

    QString error;
    const bool ok = isNew ? m_entryRepository.add(edited, &error)
                          : m_entryRepository.update(edited, &error);
    if (!ok) {
        showDatabaseError(QStringLiteral("The entry could not be saved."), error);
        return false;
    }

    // The edit may have moved the entry to another day, so follow it there.
    m_selectedDate = edited.date;
    if (edited.date.month() != m_ui->monthGrid->month()
        || edited.date.year() != m_ui->monthGrid->year()) {
        m_loadingWidgets = true;
        m_ui->comboMonth->setCurrentIndex(edited.date.month() - 1);
        m_ui->spinYear->setValue(edited.date.year());
        m_loadingWidgets = false;
        m_ui->monthGrid->setMonth(edited.date.year(), edited.date.month());
    }
    m_ui->monthGrid->setSelectedDate(edited.date);

    reloadMonth();
    reloadAllEntries();
    refreshReports();

    showStatus(isNew ? QStringLiteral("Logged %1 on %2")
                           .arg(Duration::format(edited.minutes),
                                edited.date.toString(Qt::ISODate))
                     : QStringLiteral("Entry updated"));
    return true;
}

// =====================================================================
// Calendar navigation
// =====================================================================

void MainWindow::onPreviousMonth()
{
    const QDate current(m_ui->monthGrid->year(), m_ui->monthGrid->month(), 1);
    const QDate previous = current.addMonths(-1);

    m_loadingWidgets = true;
    m_ui->comboMonth->setCurrentIndex(previous.month() - 1);
    m_ui->spinYear->setValue(previous.year());
    m_loadingWidgets = false;

    m_ui->monthGrid->setMonth(previous.year(), previous.month());
    reloadMonth();
}

void MainWindow::onNextMonth()
{
    const QDate current(m_ui->monthGrid->year(), m_ui->monthGrid->month(), 1);
    const QDate next = current.addMonths(1);

    m_loadingWidgets = true;
    m_ui->comboMonth->setCurrentIndex(next.month() - 1);
    m_ui->spinYear->setValue(next.year());
    m_loadingWidgets = false;

    m_ui->monthGrid->setMonth(next.year(), next.month());
    reloadMonth();
}

void MainWindow::onGoToToday()
{
    const QDate today = QDate::currentDate();

    m_loadingWidgets = true;
    m_ui->comboMonth->setCurrentIndex(today.month() - 1);
    m_ui->spinYear->setValue(today.year());
    m_loadingWidgets = false;

    m_selectedDate = today;
    m_ui->monthGrid->setToday(today);
    m_ui->monthGrid->setMonth(today.year(), today.month());
    m_ui->monthGrid->setSelectedDate(today);
    reloadMonth();
}

void MainWindow::onMonthComboChanged(int index)
{
    if (m_loadingWidgets || index < 0)
        return;
    m_ui->monthGrid->setMonth(m_ui->spinYear->value(), index + 1);
    reloadMonth();
}

void MainWindow::onYearSpinChanged(int year)
{
    if (m_loadingWidgets)
        return;
    m_ui->monthGrid->setMonth(year, m_ui->comboMonth->currentIndex() + 1);
    reloadMonth();
}

void MainWindow::onGridDateSelected(const QDate &date)
{
    m_selectedDate = date;
    m_ui->hoursChart->setSelectedDate(date);

    // Selecting a day in a neighbouring month moves the whole view there.
    if (date.month() != m_ui->comboMonth->currentIndex() + 1
        || date.year() != m_ui->spinYear->value()) {
        m_loadingWidgets = true;
        m_ui->comboMonth->setCurrentIndex(date.month() - 1);
        m_ui->spinYear->setValue(date.year());
        m_loadingWidgets = false;
        m_ui->monthGrid->setMonth(date.year(), date.month());
        reloadMonth();
        return;
    }

    refreshDayPanel();
}

void MainWindow::onGridDateActivated(const QDate &date)
{
    m_selectedDate = date;
    onAddEntry();
}

void MainWindow::onGridContextMenuRequested(const QDate &date, const QPoint &globalPosition)
{
    m_selectedDate = date;
    refreshDayPanel();

    // The menu is a transient pop-up built from the actions already defined in
    // the .ui file; it holds no state and displays no data of its own.
    QMenu menu(this);
    menu.addAction(m_ui->actionNewEntry);
    menu.addAction(m_ui->actionSetDayType);
    menu.addSeparator();
    menu.addAction(m_ui->actionCopyStandup);
    menu.exec(globalPosition);
}

void MainWindow::onChartDateClicked(const QDate &date)
{
    m_ui->monthGrid->setSelectedDate(date);
}

void MainWindow::onDayEntrySelectionChanged()
{
    updateEntryActions();
    refreshEntryDetail();
}

void MainWindow::onTabChanged(int)
{
    // The commands and the detail box follow whichever table is now in front.
    updateEntryActions();
    refreshEntryDetail();
    if (m_ui->tabWidget->currentWidget() == m_ui->tabReports)
        refreshReports();
}

// =====================================================================
// Entry commands
// =====================================================================

void MainWindow::onAddEntry()
{
    WorkEntry entry;
    entry.date = m_selectedDate.isValid() ? m_selectedDate : QDate::currentDate();
    entry.activity = m_settings.defaultActivity();
    entry.location = m_settings.defaultLocation();
    entry.billable = m_settings.defaultBillable();
    entry.source = EntrySource::Manual;

    editEntryInDialog(entry, true);
}

void MainWindow::onEditEntry()
{
    bool found = false;
    const WorkEntry entry = currentEntry(&found);
    if (!found) {
        showStatus(QStringLiteral("Select an entry first."));
        return;
    }
    editEntryInDialog(entry, false);
}

void MainWindow::onDuplicateEntry()
{
    bool found = false;
    WorkEntry entry = currentEntry(&found);
    if (!found) {
        showStatus(QStringLiteral("Select an entry to duplicate."));
        return;
    }

    // A copy is a new entry: it carries the details but none of the identity of
    // the original, and it is never mistaken for the imported row it came from.
    entry.id = -1;
    entry.externalId.clear();
    entry.source = EntrySource::Manual;
    entry.createdAt = QDateTime();
    entry.updatedAt = QDateTime();

    editEntryInDialog(entry, true);
}

void MainWindow::onDeleteEntry()
{
    QVector<int> ids;
    QString what;

    if (m_ui->tabWidget->currentWidget() == m_ui->tabEntries) {
        ids = selectedEntryIdsInTable();
        what = ids.size() == 1 ? QStringLiteral("this entry")
                               : QStringLiteral("these %1 entries").arg(ids.size());
    } else {
        bool found = false;
        const WorkEntry entry = currentEntry(&found);
        if (found && entry.id >= 0) {
            ids.append(entry.id);
            what = QStringLiteral("\"%1\"").arg(entry.title());
        }
    }

    if (ids.isEmpty()) {
        showStatus(QStringLiteral("Select an entry to delete."));
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Delete"),
        QStringLiteral("Delete %1? This cannot be undone.").arg(what),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!m_entryRepository.removeMany(ids, &error)) {
        showDatabaseError(QStringLiteral("The entries could not be deleted."), error);
        return;
    }

    reloadMonth();
    reloadAllEntries();
    refreshReports();
    showStatus(ids.size() == 1 ? QStringLiteral("Entry deleted")
                               : QStringLiteral("%1 entries deleted").arg(ids.size()));
}

void MainWindow::onSetDayType()
{
    if (!m_selectedDate.isValid())
        return;

    DayMeta meta = m_dayMetaRepository.forDate(m_selectedDate);
    meta.date = m_selectedDate;

    DayMetaDialog dialog(meta, m_settings, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!m_dayMetaRepository.save(dialog.dayMeta(), &error)) {
        showDatabaseError(QStringLiteral("The day could not be saved."), error);
        return;
    }

    reloadMonth();
    refreshReports();
    showStatus(QStringLiteral("%1 is now a %2")
                   .arg(m_selectedDate.toString(Qt::ISODate),
                        dayTypeDisplayName(dialog.dayMeta().type).toLower()));
}

void MainWindow::onEntriesSelectionChanged()
{
    updateEntryActions();
    refreshEntryDetail();
}

void MainWindow::onEntriesDoubleClicked(const QModelIndex &)
{
    onEditEntry();
}

void MainWindow::onEntriesContextMenuRequested(const QPoint &position)
{
    QMenu menu(this);
    menu.addAction(m_ui->actionEditEntry);
    menu.addAction(m_ui->actionDuplicateEntry);
    menu.addAction(m_ui->actionDeleteEntry);
    menu.addSeparator();
    menu.addAction(m_ui->actionChooseColumns);
    menu.addAction(m_ui->actionExportCsv);
    menu.exec(m_ui->tableEntries->viewport()->mapToGlobal(position));
}

// =====================================================================
// Filters
// =====================================================================

void MainWindow::onSearchTextChanged(const QString &text)
{
    if (m_loadingWidgets)
        return;
    m_entriesProxy.setSearchText(text);
    reloadAllEntries();
}

void MainWindow::onDateRangeToggled(bool enabled)
{
    m_ui->dateFilterFrom->setEnabled(enabled);
    m_ui->dateFilterTo->setEnabled(enabled);
    onFilterDateChanged(QDate());
}

void MainWindow::onFilterDateChanged(const QDate &)
{
    if (m_loadingWidgets)
        return;

    if (m_ui->checkDateRange->isChecked()) {
        m_entriesProxy.setDateRange(m_ui->dateFilterFrom->date(), m_ui->dateFilterTo->date());
    } else {
        m_entriesProxy.setDateRange(QDate(), QDate());
    }
    reloadAllEntries();
}

void MainWindow::onActivityFilterChanged(int)
{
    if (m_loadingWidgets)
        return;
    m_entriesProxy.setActivityFilter(m_ui->comboFilterActivity->currentData().toString());
    reloadAllEntries();
}

void MainWindow::onProjectFilterChanged(int)
{
    if (m_loadingWidgets)
        return;
    m_entriesProxy.setProjectFilter(m_ui->comboFilterProject->currentData().toString());
    reloadAllEntries();
}

void MainWindow::onSprintFilterChanged(int)
{
    if (m_loadingWidgets)
        return;
    m_entriesProxy.setSprintFilter(m_ui->comboFilterSprint->currentData().toString());
    reloadAllEntries();
}

void MainWindow::onBillableFilterChanged(int)
{
    if (m_loadingWidgets)
        return;
    m_entriesProxy.setBillableFilter(static_cast<EntryFilterProxyModel::TriState>(
        m_ui->comboFilterBillable->currentData().toInt()));
    reloadAllEntries();
}

void MainWindow::onClearFilters()
{
    m_loadingWidgets = true;
    m_ui->lineSearch->clear();
    m_ui->checkDateRange->setChecked(false);
    m_ui->dateFilterFrom->setEnabled(false);
    m_ui->dateFilterTo->setEnabled(false);
    m_ui->comboFilterActivity->setCurrentIndex(0);
    m_ui->comboFilterProject->setCurrentIndex(0);
    m_ui->comboFilterSprint->setCurrentIndex(0);
    m_ui->comboFilterBillable->setCurrentIndex(0);
    m_loadingWidgets = false;

    m_entriesProxy.clearFilters();
    reloadAllEntries();
    showStatus(QStringLiteral("Filters cleared"));
}

void MainWindow::onChooseColumns()
{
    ColumnsDialog dialog(m_visibleColumns, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_visibleColumns = dialog.selectedColumns();
    m_allEntriesModel.setColumns(m_visibleColumns);
    adjustEntryTableColumns();

    QStringList tokens;
    for (WorkEntry::Field field : m_visibleColumns)
        tokens.append(WorkEntry::fieldToken(field));
    m_settings.setVisibleEntryFields(tokens);

    showStatus(QStringLiteral("%1 columns shown").arg(m_visibleColumns.size()));
}

// =====================================================================
// Reports
// =====================================================================

void MainWindow::onReportPeriodChanged(int)
{
    if (m_loadingWidgets)
        return;

    const bool custom = m_ui->comboReportPeriod->currentData().toInt() == PeriodCustom;
    m_ui->dateReportFrom->setEnabled(custom);
    m_ui->dateReportTo->setEnabled(custom);
    refreshReports();
}

void MainWindow::onReportDateChanged(const QDate &)
{
    if (m_loadingWidgets)
        return;
    if (m_ui->comboReportPeriod->currentData().toInt() == PeriodCustom)
        refreshReports();
}

void MainWindow::onRefreshReport()
{
    refreshReports();
}

void MainWindow::onGroupByChanged(int)
{
    if (m_loadingWidgets)
        return;
    refreshReports();
}

void MainWindow::onCopyRecap()
{
    QApplication::clipboard()->setText(m_ui->textRecap->toPlainText());
    showStatus(QStringLiteral("Recap copied to the clipboard"));
}

void MainWindow::onCopyStandup()
{
    const DayMeta meta = m_dayMetaRepository.forDate(m_selectedDate);

    DaySummary summary;
    summary.date = m_selectedDate;
    summary.meta = meta;
    summary.targetMinutes = m_settings.targetMinutesFor(m_selectedDate, meta);
    for (const WorkEntry &entry : m_dayEntries)
        summary.totalMinutes += entry.minutes;

    QApplication::clipboard()->setText(TextReport::standupNote(summary, m_dayEntries));
    showStatus(QStringLiteral("Stand-up note for %1 copied to the clipboard")
                   .arg(m_selectedDate.toString(Qt::ISODate)));
}

// =====================================================================
// Import and export
// =====================================================================

void MainWindow::onExportExcel()
{
    QDate from;
    QDate to;
    if (m_ui->tabWidget->currentWidget() == m_ui->tabReports) {
        reportPeriod(&from, &to);
    } else {
        from = QDate(m_ui->monthGrid->year(), m_ui->monthGrid->month(), 1);
        to = from.addMonths(1).addDays(-1);
    }

    const QString suggested = QDir(m_settings.lastExportDirectory())
                                  .filePath(ExcelReport::suggestedFileName(from, to));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export to Excel"), suggested,
        QStringLiteral("Excel workbook (*.xlsx)"));
    if (path.isEmpty())
        return;

    const QVector<WorkEntry> entries = m_entryRepository.inRange(from, to);
    const QHash<QDate, DayMeta> dayMeta = m_dayMetaRepository.inRange(from, to);
    const QVector<DaySummary> days =
        Statistics::buildDaySummaries(from, to, entries, dayMeta, m_settings);

    ExcelReport::Options options;
    options.columns = m_visibleColumns;
    if (!m_settings.userName().isEmpty()) {
        options.title = QStringLiteral("Work log - %1 - %2 to %3")
                            .arg(m_settings.userName(),
                                 from.toString(Qt::ISODate),
                                 to.toString(Qt::ISODate));
    }

    QString error;
    if (!ExcelReport::write(path, entries, days, m_settings, options, &error)) {
        showDatabaseError(QStringLiteral("The workbook could not be written."), error);
        return;
    }

    m_settings.setLastExportDirectory(QFileInfo(path).absolutePath());
    showStatus(QStringLiteral("%1 entries exported to %2")
                   .arg(entries.size()).arg(QDir::toNativeSeparators(path)));
}

void MainWindow::onExportCsv()
{
    // The CSV follows what is on screen: the filtered rows, the chosen columns.
    QVector<WorkEntry> entries;
    for (int row = 0; row < m_entriesProxy.rowCount(); ++row) {
        const QModelIndex sourceIndex = m_entriesProxy.mapToSource(m_entriesProxy.index(row, 0));
        entries.append(m_allEntriesModel.entryAt(sourceIndex.row()));
    }

    if (entries.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export to CSV"),
                                 QStringLiteral("There is nothing to export: no entry passes "
                                                "the current filters."));
        return;
    }

    const QString suggested = QDir(m_settings.lastExportDirectory())
                                  .filePath(QStringLiteral("worklog.csv"));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export to CSV"), suggested,
        QStringLiteral("CSV file (*.csv)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!CsvIo::writeEntries(path, entries, m_visibleColumns, &error)) {
        showDatabaseError(QStringLiteral("The file could not be written."), error);
        return;
    }

    m_settings.setLastExportDirectory(QFileInfo(path).absolutePath());
    showStatus(QStringLiteral("%1 entries exported to %2")
                   .arg(entries.size()).arg(QDir::toNativeSeparators(path)));
}

void MainWindow::onImportCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import from CSV"), m_settings.lastExportDirectory(),
        QStringLiteral("CSV file (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;

    QVector<WorkEntry> entries;
    CsvIo::ImportResult result;
    QString error;
    if (!CsvIo::readEntries(path, &entries, &result, &error)) {
        showDatabaseError(QStringLiteral("The file could not be imported."), error);
        return;
    }

    if (entries.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Import from CSV"),
                                 QStringLiteral("No usable rows were found in %1.").arg(path));
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Import from CSV"),
        QStringLiteral("%1 of %2 rows can be imported.%3\n\nAdd them to the work log?")
            .arg(result.imported)
            .arg(result.rowsRead)
            .arg(result.skipped > 0
                     ? QStringLiteral("\n%1 row(s) will be skipped.").arg(result.skipped)
                     : QString()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;

    int added = 0;
    for (WorkEntry &entry : entries) {
        QString addError;
        if (m_entryRepository.add(entry, &addError))
            ++added;
        else
            result.problems.append(addError);
    }

    reloadMonth();
    reloadAllEntries();
    refreshReports();

    QString message = QStringLiteral("%1 entries imported.").arg(added);
    if (!result.problems.isEmpty()) {
        // The first few problems are enough to tell what is wrong with a file;
        // a wall of text helps nobody.
        message += QStringLiteral("\n\nNot imported:\n%1")
                       .arg(result.problems.mid(0, 12).join(QLatin1Char('\n')));
        if (result.problems.size() > 12)
            message += QStringLiteral("\n... and %1 more").arg(result.problems.size() - 12);
    }
    QMessageBox::information(this, QStringLiteral("Import from CSV"), message);
}

void MainWindow::onImportJira()
{
    const QDate suggested(m_ui->monthGrid->year(), m_ui->monthGrid->month(), 1);

    JiraImportDialog dialog(m_settings, suggested, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QVector<WorkEntry> entries = dialog.importedEntries();
    if (entries.isEmpty())
        return;

    int written = 0;
    QStringList problems;
    for (WorkEntry &entry : entries) {
        QString error;
        if (m_entryRepository.addOrUpdateByExternalId(entry, &error))
            ++written;
        else
            problems.append(QStringLiteral("%1: %2").arg(entry.issueKey, error));
    }

    reloadMonth();
    reloadAllEntries();
    refreshReports();

    QString message = QStringLiteral("%1 worklog(s) written to the work log.").arg(written);
    if (!problems.isEmpty()) {
        message += QStringLiteral("\n\nProblems:\n%1")
                       .arg(problems.mid(0, 10).join(QLatin1Char('\n')));
    }
    QMessageBox::information(this, QStringLiteral("Import from Jira"), message);
}

// =====================================================================
// The running timer
// =====================================================================

bool MainWindow::timerIsRunning() const
{
    return m_settings.timerStartedAt().isValid();
}

int MainWindow::timerElapsedSeconds() const
{
    const int accumulated = m_settings.timerAccumulatedSeconds();
    const QDateTime startedAt = m_settings.timerStartedAt();
    if (!startedAt.isValid())
        return accumulated;
    return accumulated + static_cast<int>(startedAt.secsTo(QDateTime::currentDateTime()));
}

void MainWindow::updateTimerDisplay()
{
    const bool running = timerIsRunning();
    const int elapsed = timerElapsedSeconds();

    m_ui->labelTimerElapsed->setText(QStringLiteral("%1:%2:%3")
                                         .arg(elapsed / 3600, 2, 10, QLatin1Char('0'))
                                         .arg((elapsed / 60) % 60, 2, 10, QLatin1Char('0'))
                                         .arg(elapsed % 60, 2, 10, QLatin1Char('0')));

    m_ui->buttonTimerStart->setEnabled(!running);
    m_ui->buttonTimerStop->setEnabled(running);
    m_ui->buttonTimerDiscard->setEnabled(running || elapsed > 0);
    m_ui->actionStartTimer->setEnabled(!running);
    m_ui->actionStopTimer->setEnabled(running);
    m_ui->actionDiscardTimer->setEnabled(running || elapsed > 0);

    if (running) {
        m_loadingWidgets = true;
        m_ui->comboTimerIssue->setCurrentText(m_settings.timerIssueKey());
        m_ui->lineTimerSummary->setText(m_settings.timerSummary());
        m_ui->comboTimerActivity->setCurrentIndex(
            m_ui->comboTimerActivity->findData(activityToString(m_settings.timerActivity())));
        m_loadingWidgets = false;
    }

    m_ui->dockTimer->setWindowTitle(running ? QStringLiteral("Current task - running")
                                            : QStringLiteral("Current task"));
}

void MainWindow::onTimerStart()
{
    if (timerIsRunning())
        return;

    m_settings.setTimerStartedAt(QDateTime::currentDateTime());
    m_settings.setTimerAccumulatedSeconds(0);
    m_settings.setTimerIssueKey(m_ui->comboTimerIssue->currentText().trimmed());
    m_settings.setTimerSummary(m_ui->lineTimerSummary->text().trimmed());
    m_settings.setTimerActivity(
        activityFromString(m_ui->comboTimerActivity->currentData().toString()));
    m_settings.sync();

    m_tickTimer.start();
    updateTimerDisplay();
    showStatus(QStringLiteral("Timer started"));
}

void MainWindow::onTimerStop()
{
    if (!timerIsRunning())
        return;

    const int elapsedSeconds = timerElapsedSeconds();
    m_tickTimer.stop();

    // The timer keeps its state until the entry is actually saved, so a
    // cancelled dialog does not throw the measured time away.
    WorkEntry entry;
    entry.date = QDate::currentDate();
    entry.minutes = qMax(1, elapsedSeconds / 60);
    entry.issueKey = m_ui->comboTimerIssue->currentText().trimmed();
    entry.summary = m_ui->lineTimerSummary->text().trimmed();
    entry.activity = activityFromString(m_ui->comboTimerActivity->currentData().toString());
    entry.location = m_settings.defaultLocation();
    entry.billable = m_settings.defaultBillable();
    entry.source = EntrySource::Timer;

    const QDateTime startedAt = m_settings.timerStartedAt();
    if (startedAt.isValid()) {
        entry.date = startedAt.date();
        entry.startTime = startedAt.time();
        entry.endTime = QDateTime::currentDateTime().time();
    }

    if (entry.summary.isEmpty() && entry.issueKey.isEmpty())
        entry.summary = QStringLiteral("Timed work");

    if (!editEntryInDialog(entry, true)) {
        // Saving was cancelled: keep counting where it left off.
        m_settings.setTimerAccumulatedSeconds(elapsedSeconds);
        m_settings.setTimerStartedAt(QDateTime::currentDateTime());
        m_settings.sync();
        m_tickTimer.start();
        updateTimerDisplay();
        return;
    }

    m_settings.setTimerStartedAt(QDateTime());
    m_settings.setTimerAccumulatedSeconds(0);
    m_settings.setTimerSummary(QString());
    m_settings.sync();

    m_loadingWidgets = true;
    m_ui->lineTimerSummary->clear();
    m_loadingWidgets = false;

    updateTimerDisplay();
}

void MainWindow::onTimerDiscard()
{
    if (!timerIsRunning() && timerElapsedSeconds() == 0)
        return;

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Discard the timer"),
        QStringLiteral("Throw away the %1 measured so far?")
            .arg(Duration::format(timerElapsedSeconds() / 60)),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    m_tickTimer.stop();
    m_settings.setTimerStartedAt(QDateTime());
    m_settings.setTimerAccumulatedSeconds(0);
    m_settings.sync();
    updateTimerDisplay();
    showStatus(QStringLiteral("Timer discarded"));
}

void MainWindow::onTimerTick()
{
    updateTimerDisplay();
}

// =====================================================================
// Miscellaneous
// =====================================================================

void MainWindow::onRefresh()
{
    reloadMonth();
    reloadAllEntries();
    refreshReports();
    showStatus(QStringLiteral("Reloaded"));
}

void MainWindow::onShowWeekendsToggled(bool show)
{
    if (m_loadingWidgets)
        return;

    m_settings.setShowWeekends(show);
    m_settings.sync();

    // The grid rebuilds its columns from the settings when the month is set.
    const int year = m_ui->monthGrid->year();
    const int month = m_ui->monthGrid->month();
    m_ui->monthGrid->setSettings(&m_settings);
    m_ui->monthGrid->setMonth(year, month);
    m_ui->monthGrid->update();
    reloadMonth();
}

void MainWindow::onOpenSettings()
{
    SettingsDialog dialog(m_settings, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_loadingWidgets = true;
    m_ui->actionShowWeekends->setChecked(m_settings.showWeekends());
    m_loadingWidgets = false;

    applySettingsToViews();

    const int year = m_ui->monthGrid->year();
    const int month = m_ui->monthGrid->month();
    m_ui->monthGrid->setSettings(&m_settings);
    m_ui->monthGrid->setMonth(year, month);

    reloadMonth();
    reloadAllEntries();
    refreshReports();
    showStatus(QStringLiteral("Settings saved"));
}

void MainWindow::onAbout()
{
    AboutDialog dialog(Database::defaultPath(), this);
    dialog.exec();
}

void MainWindow::onAboutQt()
{
    QMessageBox::aboutQt(this, QStringLiteral("Work Calendar"));
}
