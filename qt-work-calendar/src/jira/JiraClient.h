#ifndef WORKCALENDAR_JIRACLIENT_H
#define WORKCALENDAR_JIRACLIENT_H

#include "core/WorkEntry.h"

#include <QDate>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QNetworkAccessManager;

/*! Everything needed to talk to one Jira server. */
struct JiraConfig
{
    QString baseUrl;            //!< "https://jira.example.com", no trailing slash
    QString username;           //!< also the worklog author that is imported
    QString token;              //!< password or API token

    QString sprintField = QStringLiteral("customfield_10005");
    QString testsheetKeyword = QStringLiteral("testsheet");
    QString mergeRequestMarker = QStringLiteral("/merge_requests/");

    bool isUsable(QString *reason = nullptr) const;
};

/*! What an import produced. */
struct JiraImportResult
{
    QVector<WorkEntry> entries;
    int issuesScanned = 0;
    int worklogsMatched = 0;
    QStringList messages;   //!< progress and warnings, shown in the import log
    bool cancelled = false;
};

/*!
 * \brief Reads worklogs from Jira and turns them into work entries.
 *
 * The flow mirrors the REST calls a reporting script makes by hand:
 *
 *   1. search for the issues the user booked time on in the period
 *   2. for each issue, list its remote links to find the merge request
 *   3. read the issue's worklogs and keep the user's own, inside the period
 *
 * \par Why the calls block
 * Each request is awaited on a local event loop before the next one starts.
 * The import runs from a modal dialog that has nothing else to do, the server
 * must be asked one issue at a time anyway, and a linear function is far easier
 * to follow - and to fix - than a chain of asynchronous callbacks. The dialog
 * stays repainted because the loop keeps processing events, and cancel() ends
 * the run at the next request boundary.
 */
class JiraClient : public QObject
{
    Q_OBJECT

public:
    explicit JiraClient(QObject *parent = nullptr);

    void setConfig(const JiraConfig &config);
    JiraConfig config() const;

    /*!
     * \brief Imports every worklog the user booked between \a from and \a to.
     *
     * \param[out] result  the entries and a log of what happened
     * \param[out] error   set when the import could not run at all
     * \return false on a hard failure; a partially cancelled run returns true
     */
    bool fetchWorklogs(const QDate &from, const QDate &to,
                       JiraImportResult *result, QString *error = nullptr);

    /*! Asks a running import to stop at the next issue. */
    void cancel();

signals:
    /*! \a done of \a total issues processed; \a message describes the step. */
    void progressChanged(int done, int total, const QString &message);

private:
    /*!
     * \brief Performs one GET and returns the body.
     * \param path   the path after the base URL, starting with a slash
     * \param query  encoded query string, without the leading "?"
     */
    QByteArray get(const QString &path, const QString &query, QString *error);

    /*! Builds the entry for one issue and one worklog. */
    WorkEntry entryFromWorklog(const class QJsonObject &issue,
                               const class QJsonObject &worklog,
                               const QString &mergeRequestUrl) const;

    /*! Reads the sprint name out of the custom field, which is often a string. */
    QString sprintNameOf(const class QJsonObject &fields) const;

    /*! The first attachment whose file name mentions the testsheet keyword. */
    void findTestsheet(const class QJsonObject &fields,
                       QString *name, QString *url) const;

    /*! The first remote link that looks like a merge request. */
    QString findMergeRequest(const QString &issueKey);

    QNetworkAccessManager *m_network;
    JiraConfig m_config;
    bool m_cancelRequested = false;
};

#endif // WORKCALENDAR_JIRACLIENT_H
