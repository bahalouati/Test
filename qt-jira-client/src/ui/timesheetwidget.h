#pragma once

#include "core/timesheet.h"
#include "core/timesheetloader.h"

#include <QDate>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class TimesheetWidget; }
QT_END_NAMESPACE

namespace jira { class Client; }

// The month view: a Mon-Fri calendar where a short day is coloured, plus the
// flat worklog table underneath it. This is the screen that answers "which days
// am I still missing hours on?" without adding anything up by hand.
//
// The layout lives in timesheetwidget.ui. The constructor takes only a parent
// so the class can be promoted in Qt Designer; the client arrives afterwards
// through setClient().
class TimesheetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimesheetWidget(QWidget *parent = nullptr);
    ~TimesheetWidget() override;

    void setClient(jira::Client *client);
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
    void showCalendarMenu(const QPoint &position);
    void toggleHoliday();
    void cellActivated(int row, int column);

private:
    void buildCalendar();
    void buildTable();
    void updateSummary();
    QDate firstOfMonth() const;
    QDate lastOfMonth() const;
    // The day under the calendar's current cell, or an invalid date.
    QDate selectedDay() const;

    Ui::TimesheetWidget *ui;
    jira::Client *m_client = nullptr;
    jira::TimesheetLoader *m_loader = nullptr;
    jira::TimesheetSettings m_settings;
    jira::HolidayCalendar m_holidays;
    jira::User m_me;

    QList<jira::TimesheetEntry> m_entries;
    QList<jira::DaySummary> m_days;

};
