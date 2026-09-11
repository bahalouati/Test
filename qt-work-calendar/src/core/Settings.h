#ifndef WORKCALENDAR_SETTINGS_H
#define WORKCALENDAR_SETTINGS_H

#include "core/DayMeta.h"
#include "core/Duration.h"
#include "core/Enums.h"

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVector>

/*!
 * \brief Every user preference, read from and written to QSettings.
 *
 * The class is a thin typed front for QSettings: each preference has a getter,
 * a setter and a documented default, so no key string is ever spelled out
 * twice and no default is ever guessed at the call site.
 *
 * One instance lives in MainWindow and is handed to whatever needs it. It is
 * cheap to copy the values out; the object itself is not copied around.
 */
class Settings
{
public:
    Settings();

    /*! Writes anything still held in memory to disk. */
    void sync();

    // ------------------------------------------------------------- targets
    /*! Minutes expected on a normal working day. Default 8 hours. */
    int dailyTargetMinutes() const;
    void setDailyTargetMinutes(int minutes);

    /*!
     * \brief At or above this, an incomplete day counts as "nearly there".
     *
     * Below it the day is flagged as clearly short. Default 6 hours, matching
     * the green/yellow/red split of the spreadsheet this tool replaces.
     */
    int warningThresholdMinutes() const;
    void setWarningThresholdMinutes(int minutes);

    /*! The days of the week that carry a target. Qt::Monday ... Qt::Sunday. */
    QSet<int> workingDays() const;
    void setWorkingDays(const QSet<int> &days);

    bool isWorkingDay(const QDate &date) const;

    /*!
     * \brief The target for \a date once the day's own record is taken into account.
     *
     * Precedence: an explicit override, then the day type (absences expect
     * nothing, a half day expects half), then the weekly working-day pattern.
     */
    int targetMinutesFor(const QDate &date, const DayMeta &meta) const;

    // ------------------------------------------------------------ defaults
    Activity defaultActivity() const;
    void setDefaultActivity(Activity activity);

    WorkLocation defaultLocation() const;
    void setDefaultLocation(WorkLocation location);

    bool defaultBillable() const;
    void setDefaultBillable(bool billable);

    /*! New durations are rounded up to this many minutes. 0 disables it. */
    int roundingIncrementMinutes() const;
    void setRoundingIncrementMinutes(int minutes);

    /*! The working day people usually start at, used to prefill the dialog. */
    QTime defaultStartTime() const;
    void setDefaultStartTime(const QTime &time);

    // ----------------------------------------------------------- appearance
    Duration::Format durationFormat() const;
    void setDurationFormat(Duration::Format format);

    /*! Show Saturday and Sunday columns in the month grid. */
    bool showWeekends() const;
    void setShowWeekends(bool show);

    /*! How many entry lines a calendar cell lists before it says "+n more". */
    int maxLinesPerDay() const;
    void setMaxLinesPerDay(int lines);

    /*! Colour of a day in the given state. */
    QColor dayColor(int state) const;
    void setDayColor(int state, const QColor &color);
    QColor defaultDayColor(int state) const;

    // ---------------------------------------------------------------- Jira
    QString jiraBaseUrl() const;
    void setJiraBaseUrl(const QString &url);

    QString jiraUsername() const;
    void setJiraUsername(const QString &username);

    /*!
     * \brief The Jira password or API token.
     *
     * QSettings is not a secret store: on Linux this ends up readable in
     * ~/.config. Leave it empty to be asked once per import instead.
     */
    QString jiraToken() const;
    void setJiraToken(const QString &token);

    /*! Custom field holding the sprint, "customfield_10005" on most servers. */
    QString jiraSprintField() const;
    void setJiraSprintField(const QString &field);

    /*! Attachments whose file name contains this word are taken as testsheets. */
    QString jiraTestsheetKeyword() const;
    void setJiraTestsheetKeyword(const QString &keyword);

    /*! Remote links whose URL contains this fragment are taken as merge requests. */
    QString jiraMergeRequestMarker() const;
    void setJiraMergeRequestMarker(const QString &marker);

    // -------------------------------------------------------------- general
    QString userName() const;
    void setUserName(const QString &name);

    QString lastExportDirectory() const;
    void setLastExportDirectory(const QString &path);

    /*! Column layout of the entry table, as field tokens. */
    QStringList visibleEntryFields() const;
    void setVisibleEntryFields(const QStringList &tokens);

    QByteArray mainWindowGeometry() const;
    void setMainWindowGeometry(const QByteArray &geometry);

    QByteArray mainWindowState() const;
    void setMainWindowState(const QByteArray &state);

    QByteArray entryTableHeaderState() const;
    void setEntryTableHeaderState(const QByteArray &state);

    // --------------------------------------------------------- running timer
    /*! The instant a running timer was started, or an invalid date-time. */
    QDateTime timerStartedAt() const;
    void setTimerStartedAt(const QDateTime &startedAt);

    /*! Seconds accumulated by a timer that is currently paused. */
    int timerAccumulatedSeconds() const;
    void setTimerAccumulatedSeconds(int seconds);

    QString timerIssueKey() const;
    void setTimerIssueKey(const QString &issueKey);

    QString timerSummary() const;
    void setTimerSummary(const QString &summary);

    Activity timerActivity() const;
    void setTimerActivity(Activity activity);

private:
    /*! Reads \a key, returning \a fallback when it is missing or unreadable. */
    int readInt(const QString &key, int fallback) const;
    QString readString(const QString &key, const QString &fallback) const;
    bool readBool(const QString &key, bool fallback) const;
    void write(const QString &key, const QVariant &value);

    mutable QSettings m_settings;
};

#endif // WORKCALENDAR_SETTINGS_H
