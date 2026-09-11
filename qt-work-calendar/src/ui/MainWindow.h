#ifndef WORKCALENDAR_MAINWINDOW_H
#define WORKCALENDAR_MAINWINDOW_H

#include "core/DaySummary.h"
#include "core/Settings.h"
#include "core/WorkEntry.h"
#include "data/DayMetaRepository.h"
#include "data/Database.h"
#include "data/WorkEntryRepository.h"
#include "model/BreakdownTableModel.h"
#include "model/EntryFilterProxyModel.h"
#include "model/WorkEntryTableModel.h"
#include "ui/EntryDialog.h"

#include <QDate>
#include <QMainWindow>
#include <QTimer>
#include <QVector>

namespace Ui { class MainWindow; }

/*!
 * \brief The application window: the calendar, the entry table and the reports.
 *
 * \par How the data flows
 * The window is the only thing that talks to the repositories. It reads what a
 * view needs, hands it over, and reads again after every change. Nothing edits
 * the database behind its back, so "why does the table say that?" always has
 * one answer: because reload() last put it there.
 *
 * \par Where the widgets come from
 * Every widget is declared in MainWindow.ui and created by setupUi(). The two
 * custom views - the month grid and the bar chart - are promoted widgets, so
 * they are laid out in Qt Designer like any other. Nothing in this class builds
 * a widget to display a row of data; models and item data do that work.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /*!
     * \brief Opens the database and fills the views.
     * \param databasePath  the file to use; empty means the standard location
     * \return false when the database could not be opened; the caller should
     *         then not show the window
     */
    bool initialise(const QString &databasePath = QString());

protected:
    /*! Saves the window layout, and warns about a timer that is still running. */
    void closeEvent(QCloseEvent *event) override;

private slots:
    // ---- calendar navigation
    void onPreviousMonth();
    void onNextMonth();
    void onGoToToday();
    void onMonthComboChanged(int index);
    void onYearSpinChanged(int year);
    void onGridDateSelected(const QDate &date);
    void onGridDateActivated(const QDate &date);
    void onGridContextMenuRequested(const QDate &date, const QPoint &globalPosition);
    void onChartDateClicked(const QDate &date);
    void onDayEntrySelectionChanged();

    // ---- entries
    void onAddEntry();
    void onEditEntry();
    void onDuplicateEntry();
    void onDeleteEntry();
    void onSetDayType();
    void onEntriesSelectionChanged();
    void onEntriesDoubleClicked(const QModelIndex &index);
    void onEntriesContextMenuRequested(const QPoint &position);
    void onTabChanged(int index);

    // ---- filters
    void onSearchTextChanged(const QString &text);
    void onDateRangeToggled(bool enabled);
    void onFilterDateChanged(const QDate &date);
    void onActivityFilterChanged(int index);
    void onProjectFilterChanged(int index);
    void onSprintFilterChanged(int index);
    void onBillableFilterChanged(int index);
    void onClearFilters();
    void onChooseColumns();

    // ---- reports
    void onReportPeriodChanged(int index);
    void onReportDateChanged(const QDate &date);
    void onRefreshReport();
    void onGroupByChanged(int index);
    void onCopyRecap();
    void onCopyStandup();

    // ---- import and export
    void onExportExcel();
    void onExportCsv();
    void onImportCsv();
    void onImportJira();

    // ---- timer
    void onTimerStart();
    void onTimerStop();
    void onTimerDiscard();
    void onTimerTick();

    // ---- miscellaneous
    void onRefresh();
    void onShowWeekendsToggled(bool show);
    void onOpenSettings();
    void onAbout();
    void onAboutQt();

private:
    // ---- setup, called once from the constructor
    void setupModels();
    void setupCalendarTab();
    void setupEntriesTab();
    void setupReportsTab();
    void setupTimerDock();
    void setupConnections();
    void restoreLayout();
    void saveLayout();

    // ---- reloading
    /*! Re-reads the shown month and refreshes the calendar, chart and day panel. */
    void reloadMonth();

    /*! Re-reads every entry into the entry table. */
    void reloadAllEntries();

    /*! Refreshes the right-hand panel for the selected day. */
    void refreshDayPanel();

    /*! Refreshes the detail box under the day's entry list. */
    void refreshEntryDetail();

    /*!
     * \brief Enables the entry commands only when there is an entry to act on.
     *
     * "Which entry?" depends on the tab in front of the user, so this is
     * re-run whenever the selection or the tab changes.
     */
    void updateEntryActions();

    /*! Recomputes the report tab from its period. */
    void refreshReports();

    /*! Refreshes the filter drop-downs from the values actually in use. */
    void refreshFilterChoices();

    /*! Refreshes the one-line summary next to the month navigation. */
    void refreshMonthSummary();

    /*! Applies the settings to the views after they may have changed. */
    void applySettingsToViews();

    /*!
     * \brief Sizes the entry table's columns to their contents.
     *
     * Long values are capped so that one enormous note cannot push every other
     * column off the screen, and the summary takes whatever room is left, since
     * that is the column people actually read.
     */
    void adjustEntryTableColumns();

    // ---- helpers
    /*! The entry currently selected, wherever the selection lives. */
    WorkEntry currentEntry(bool *found = nullptr) const;

    /*! The ids of every selected row of the entry table. */
    QVector<int> selectedEntryIdsInTable() const;

    /*! Fills in the drop-down contents an entry dialog should offer. */
    EntrySuggestions buildSuggestions() const;

    /*! Opens the entry dialog for \a entry; returns true when it was saved. */
    bool editEntryInDialog(WorkEntry entry, bool isNew);

    /*! Shows \a message in the status bar for a few seconds. */
    void showStatus(const QString &message);

    /*! Reports a failed database operation. */
    void showDatabaseError(const QString &whatFailed, const QString &error);

    /*! The period the report tab currently covers. */
    void reportPeriod(QDate *from, QDate *to) const;

    /*! Refreshes the timer widgets from the stored timer state. */
    void updateTimerDisplay();

    /*! Seconds the running timer has accumulated so far. */
    int timerElapsedSeconds() const;

    /*! True when a timer is currently running. */
    bool timerIsRunning() const;

    Ui::MainWindow *m_ui;

    Database m_database;
    WorkEntryRepository m_entryRepository;
    DayMetaRepository m_dayMetaRepository;
    Settings m_settings;

    WorkEntryTableModel m_allEntriesModel;   //!< every entry, for the Entries tab
    EntryFilterProxyModel m_entriesProxy;
    WorkEntryTableModel m_dayEntriesModel;   //!< the selected day, for the calendar tab
    BreakdownTableModel m_breakdownModel;

    QTimer m_tickTimer;                      //!< drives the running-timer display

    QDate m_selectedDate;
    QVector<WorkEntry> m_monthEntries;
    QVector<DaySummary> m_monthDays;
    QVector<WorkEntry> m_dayEntries;
    QVector<WorkEntry::Field> m_visibleColumns;

    /*! Set while the code is filling widgets, so their signals change nothing. */
    bool m_loadingWidgets = false;
};

#endif // WORKCALENDAR_MAINWINDOW_H
