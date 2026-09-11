#include "tasktracker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTimer>

namespace jira {

qint64 TrackedTask::elapsedSeconds(const QDateTime &now) const
{
    if (!runningSince.isValid())
        return trackedSeconds;
    const qint64 running = runningSince.secsTo(now);
    return trackedSeconds + qMax(qint64(0), running);
}

QJsonObject TrackedTask::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("issueKey"), issueKey);
    object.insert(QStringLiteral("summary"), summary);
    object.insert(QStringLiteral("sprint"), sprint);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("trackedSeconds"), double(trackedSeconds));
    object.insert(QStringLiteral("unassigned"), unassigned);
    return object;
}

TrackedTask TrackedTask::fromJson(const QJsonObject &object)
{
    TrackedTask task;
    task.issueKey = object.value(QStringLiteral("issueKey")).toString();
    task.summary = object.value(QStringLiteral("summary")).toString();
    task.sprint = object.value(QStringLiteral("sprint")).toString();
    task.status = object.value(QStringLiteral("status")).toString();
    task.trackedSeconds = qint64(object.value(QStringLiteral("trackedSeconds")).toDouble());
    task.unassigned = object.value(QStringLiteral("unassigned")).toBool();
    // runningSince is deliberately not persisted: see load().
    return task;
}

QString formatStopwatch(qint64 seconds)
{
    const qint64 safe = qMax(qint64(0), seconds);
    return QStringLiteral("%1:%2:%3")
            .arg(safe / 3600)
            .arg((safe % 3600) / 60, 2, 10, QLatin1Char('0'))
            .arg(safe % 60, 2, 10, QLatin1Char('0'));
}

QString formatTrackedDuration(qint64 seconds)
{
    const qint64 minutes = qMax(qint64(0), seconds) / 60;
    const qint64 hours = minutes / 60;
    const qint64 remainder = minutes % 60;
    if (hours > 0 && remainder > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(remainder, 2, 10, QLatin1Char('0'));
    if (hours > 0)
        return QStringLiteral("%1h").arg(hours);
    return QStringLiteral("%1m").arg(remainder);
}

TaskTracker::TaskTracker(QObject *parent)
    : QObject(parent)
    , m_ticker(new QTimer(this))
{
    m_ticker->setInterval(1000);
    connect(m_ticker, &QTimer::timeout, this, &TaskTracker::tick);
}

QString TaskTracker::storagePath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return directory + QStringLiteral("/tasks.json");
}

int TaskTracker::indexOf(const QString &issueKey) const
{
    for (int index = 0; index < m_tasks.size(); ++index) {
        if (m_tasks.at(index).issueKey == issueKey)
            return index;
    }
    return -1;
}

TrackedTask TaskTracker::currentTask() const
{
    const int index = indexOf(m_currentKey);
    return index < 0 ? TrackedTask() : m_tasks.at(index);
}

void TaskTracker::bankRunningTime()
{
    const int index = indexOf(m_currentKey);
    if (index < 0)
        return;
    TrackedTask &task = m_tasks[index];
    if (task.isRunning()) {
        task.trackedSeconds = task.elapsedSeconds();
        task.runningSince = QDateTime();
    }
}

void TaskTracker::start(const QString &issueKey)
{
    const int index = indexOf(issueKey);
    if (index < 0)
        return;

    // Exactly one task runs at a time; the previous one banks what it has.
    bankRunningTime();

    m_currentKey = issueKey;
    m_tasks[index].runningSince = QDateTime::currentDateTime();
    m_ticker->start();
    save();
    emit changed();
}

void TaskTracker::stop()
{
    bankRunningTime();
    m_ticker->stop();
    save();
    emit changed();
}

void TaskTracker::toggle(const QString &issueKey)
{
    const int index = indexOf(issueKey);
    if (index < 0)
        return;
    if (m_tasks.at(index).isRunning())
        stop();
    else
        start(issueKey);
}

void TaskTracker::resetTracked(const QString &issueKey)
{
    const int index = indexOf(issueKey);
    if (index < 0)
        return;
    if (m_tasks.at(index).isRunning())
        stop();
    m_tasks[index].trackedSeconds = 0;
    save();
    emit changed();
}

void TaskTracker::remove(const QString &issueKey)
{
    const int index = indexOf(issueKey);
    if (index < 0)
        return;
    if (m_currentKey == issueKey) {
        bankRunningTime();
        m_ticker->stop();
        m_currentKey.clear();
    }
    m_tasks.removeAt(index);
    save();
    emit changed();
}

void TaskTracker::merge(const QList<Issue> &sprintIssues, const QSet<QString> &stillMine)
{
    // Add anything new the sprint turned up.
    for (const Issue &issue : sprintIssues) {
        if (issue.key.isEmpty() || !stillMine.contains(issue.key))
            continue;
        const int index = indexOf(issue.key);
        if (index >= 0) {
            m_tasks[index].summary = issue.summary;
            m_tasks[index].sprint = issue.sprint;
            m_tasks[index].status = issue.status;
            m_tasks[index].unassigned = false;
            continue;
        }
        TrackedTask task;
        task.issueKey = issue.key;
        task.summary = issue.summary;
        task.sprint = issue.sprint;
        task.status = issue.status;
        m_tasks.append(task);
    }

    // A task Jira no longer assigns to me is done. Drop it -- unless it is
    // holding time that has not been written into Jira yet, in which case
    // deleting it would throw that away silently; flag it instead and let the
    // list offer a dismiss.
    QSet<QString> seen;
    for (const Issue &issue : sprintIssues)
        seen.insert(issue.key);

    for (int index = m_tasks.size() - 1; index >= 0; --index) {
        TrackedTask &task = m_tasks[index];
        // Not in the response at all: could be a permission blip or the issue
        // moving out of the sprint. Neither means "done", so leave it be.
        if (!seen.contains(task.issueKey))
            continue;
        if (stillMine.contains(task.issueKey))
            continue;

        // Holding time, or being timed right now -- either way, deleting it
        // would throw away work. A task started seconds ago has no elapsed
        // whole seconds yet, so the running check is not redundant.
        if (task.elapsedSeconds() > 0 || task.isRunning()) {
            task.unassigned = true;
            continue;
        }
        if (m_currentKey == task.issueKey) {
            m_ticker->stop();
            m_currentKey.clear();
        }
        m_tasks.removeAt(index);
    }

    save();
    emit changed();
}

void TaskTracker::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();

    m_tasks.clear();
    for (const QJsonValue &entry : document.object().value(QStringLiteral("tasks")).toArray()) {
        const TrackedTask task = TrackedTask::fromJson(entry.toObject());
        if (!task.issueKey.isEmpty())
            m_tasks.append(task);
    }

    // A timer that was running when the application closed is not resumed. The
    // alternative -- counting the time the machine was off -- would quietly
    // book a whole night against a task.
    m_currentKey.clear();
    emit changed();
}

void TaskTracker::save() const
{
    const QString path = storagePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray array;
    for (const TrackedTask &task : m_tasks) {
        TrackedTask snapshot = task;
        // Persist the running task's time as banked, so a crash costs at most
        // the current second rather than the whole run.
        snapshot.trackedSeconds = task.elapsedSeconds();
        array.append(snapshot.toJson());
    }

    QJsonObject root;
    root.insert(QStringLiteral("tasks"), array);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

} // namespace jira
