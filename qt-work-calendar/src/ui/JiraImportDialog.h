#ifndef WORKCALENDAR_JIRAIMPORTDIALOG_H
#define WORKCALENDAR_JIRAIMPORTDIALOG_H

#include "core/Settings.h"
#include "core/WorkEntry.h"
#include "jira/JiraClient.h"

#include <QDialog>
#include <QVector>

namespace Ui { class JiraImportDialog; }

/*!
 * \brief Asks for a server, a period and credentials, then runs the import.
 *
 * The dialog only produces entries; writing them to the database is the
 * window's job, which keeps the decision of what to do with an import in one
 * place.
 */
class JiraImportDialog : public QDialog
{
    Q_OBJECT

public:
    JiraImportDialog(Settings &settings, const QDate &suggestedMonth, QWidget *parent = nullptr);
    ~JiraImportDialog() override;

    /*! What the last successful run brought back. */
    QVector<WorkEntry> importedEntries() const;

private slots:
    void onImportClicked();
    void onCancelClicked();
    void onThisMonthClicked();
    void onLastMonthClicked();

    /*! Moves the progress bar and appends the step to the log. */
    void onProgressChanged(int done, int total, const QString &message);

private:
    /*! Appends one line to the log and keeps it scrolled to the end. */
    void appendLog(const QString &line);

    /*! Sets the period to the month containing \a anyDayOfMonth. */
    void setMonth(const QDate &anyDayOfMonth);

    /*! Enables or disables the controls while a run is in progress. */
    void setBusy(bool busy);

    Ui::JiraImportDialog *m_ui;
    Settings &m_settings;
    JiraClient m_client;
    QVector<WorkEntry> m_importedEntries;
    bool m_busy = false;
};

#endif // WORKCALENDAR_JIRAIMPORTDIALOG_H
