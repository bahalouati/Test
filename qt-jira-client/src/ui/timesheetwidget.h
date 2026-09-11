#pragma once

#include "core/timesheet.h"
#include "core/timesheetloader.h"

#include <QDate>
#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

namespace jira { class Client; }

// The month view: a Mon-Fri calendar where a short day is coloured, plus the
// flat worklog table underneath it. This is the screen that answers "which days
// am I still missing hours on?" without adding anything up by hand.
class TimesheetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimesheetWidget(jira::Client *client, QWidget *parent = nullptr);

    void setIdentity(const jira::User &me);
    void refresh();

signals:
    void statusMessage(const QString &message);
    void errorOccurred(const QString &message);
    void issueActivated(const QString &issueKey);

private slots:
    void monthChanged();
    void loadFinished(const QList<jira::TimesheetEntry> &entries);
    void loadFailed(const QString &message);
    void loadProgress(int done, int total, const QString &message);
    void exportCsv();
    void exportXlsx();
    void cellActivated(int row, int column);

private:
    void buildCalendar();
    void buildTable();
    void updateSummary();
    QDate firstOfMonth() const;
    QDate lastOfMonth() const;

    jira::Client *m_client;
    jira::TimesheetLoader *m_loader;
    jira::TimesheetSettings m_settings;
    jira::User m_me;

    QList<jira::TimesheetEntry> m_entries;
    QList<jira::DaySummary> m_days;

    QComboBox *m_month;
    QComboBox *m_year;
    QPushButton *m_refresh;
    QPushButton *m_export;
    QPushButton *m_exportXlsx;
    QLabel *m_summary;
    QLabel *m_legend;
    QProgressBar *m_progress;
    QTableWidget *m_calendar;
    QTableWidget *m_table;
};
