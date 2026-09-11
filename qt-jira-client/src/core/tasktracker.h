#pragma once

#include "jiratypes.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QJsonObject>
#include <QSet>
#include <QString>

class QTimer;

namespace jira {

// A task I am working on this sprint, with the time I have spent on it measured
// locally. Nothing here is ever pushed to Jira -- the tracked figure is a
// reminder for when the real worklog gets written by hand.
struct TrackedTask {
    QString issueKey;
    QString summary;
    QString sprint;
    QString status;

    // Time banked from finished runs of the timer.
    qint64 trackedSeconds = 0;
    // Set while the timer is running on this task; invalid otherwise.
    QDateTime runningSince;

    // True once Jira says the issue is no longer assigned to me. Such a task is
    // done and drops off the list -- unless it still holds unlogged time, which
    // would be silently thrown away.
    bool unassigned = false;

    bool isRunning() const { return runningSince.isValid(); }
    // Banked time plus whatever the current run has accumulated so far.
    qint64 elapsedSeconds(const QDateTime &now = QDateTime::currentDateTime()) const;

    QJsonObject toJson() const;
    static TrackedTask fromJson(const QJsonObject &object);
};

// "1:05:22"
QString formatStopwatch(qint64 seconds);
// "1h 05m" -- what you would type into Jira's own log-work field.
QString formatTrackedDuration(qint64 seconds);

// The task list and its stopwatch. Exactly one task runs at a time: starting
// another stops the one before it.
class TaskTracker : public QObject
{
    Q_OBJECT

public:
    explicit TaskTracker(QObject *parent = nullptr);

    QList<TrackedTask> tasks() const { return m_tasks; }
    int indexOf(const QString &issueKey) const;
    QString currentIssueKey() const { return m_currentKey; }
    TrackedTask currentTask() const;

    void start(const QString &issueKey);
    void stop();
    void toggle(const QString &issueKey);

    // Wipes the banked time for one task, for after it has been logged in Jira.
    void resetTracked(const QString &issueKey);
    // Takes the task off the list, tracked time and all.
    void remove(const QString &issueKey);

    // Folds a fresh look at the sprint into the list: new issues are added,
    // issues Jira no longer assigns to me are marked unassigned, and those with
    // no tracked time are dropped outright.
    void merge(const QList<Issue> &sprintIssues, const QSet<QString> &stillMine);

    void load();
    void save() const;

    static QString storagePath();

signals:
    void changed();
    // Emitted once a second while a task is running, for the elapsed display.
    void tick();

private:
    void bankRunningTime();

    QList<TrackedTask> m_tasks;
    QString m_currentKey;
    QTimer *m_ticker;
};

} // namespace jira
