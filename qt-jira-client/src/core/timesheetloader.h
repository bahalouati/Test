#pragma once

#include "jiraclient.h"
#include "timesheet.h"

#include <QDate>
#include <QHash>
#include <QObject>
#include <QQueue>

namespace jira {

// Runs the whole pipeline behind the month view: one search for the issues I
// booked time against, then per-issue worklogs (and remote links, for the merge
// request) with a bounded number of requests in flight.
class TimesheetLoader : public QObject
{
    Q_OBJECT

public:
    explicit TimesheetLoader(Client *client, QObject *parent = nullptr);

    // `me` comes from /myself and decides which worklogs are mine. Passing a
    // null user falls back to currentUser() in the JQL and keeps every worklog
    // the search returns.
    void load(const QDate &from, const QDate &to, const User &me, const TimesheetSettings &settings);
    void cancel();
    bool isRunning() const { return m_running; }

    // Exposed so the JQL can be shown to the user and copied into Jira.
    static QString buildJql(const QDate &from, const QDate &to, const User &me);

signals:
    void progress(int done, int total, const QString &message);
    void finished(const QList<TimesheetEntry> &entries);
    void failed(const QString &message);

private:
    struct PendingIssue {
        Issue issue;
        bool worklogsDone = false;
        bool linksDone = false;
        QString mergeRequestUrl;
        QList<Worklog> worklogs;
    };

    void startIssue(int index);
    void pumpQueue();
    void issueSettled(int index);
    bool isMine(const User &author) const;

    Client *m_client;
    TimesheetSettings m_settings;
    User m_me;
    QDate m_from;
    QDate m_to;

    QList<PendingIssue> m_pending;
    QQueue<int> m_queue;
    int m_inFlight = 0;
    int m_completed = 0;
    bool m_running = false;
};

} // namespace jira
