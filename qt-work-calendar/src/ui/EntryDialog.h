#ifndef WORKCALENDAR_ENTRYDIALOG_H
#define WORKCALENDAR_ENTRYDIALOG_H

#include "core/Settings.h"
#include "core/WorkEntry.h"

#include <QDate>
#include <QDialog>
#include <QMap>
#include <QStringList>
#include <QVector>

namespace Ui { class EntryDialog; }

/*!
 * \brief What the dialog offers the user as ready-made choices.
 *
 * The window fills this in from the database before opening the dialog, which
 * keeps the dialog free of any storage knowledge and makes it testable with a
 * handful of strings.
 */
struct EntrySuggestions
{
    /*! Recently worked issues; picking one copies its details into the form. */
    QVector<WorkEntry> recentIssues;

    QStringList issueTypes;
    QStringList epics;
    QStringList sprints;
    QStringList components;
    QStringList fixVersions;
    QStringList branches;

    /*! Minutes already logged per day, so "Fill the day" knows what is missing. */
    QMap<QDate, int> loggedMinutesByDate;
};

/*!
 * \brief Creates or edits one work entry, with every field it can carry.
 *
 * The fields are grouped over four tabs - the work itself, the issue it
 * belongs to, its links, and the notes - so the common case stays a date, a
 * duration and an issue key, while nothing is lost for the days that need more.
 */
class EntryDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param entry        the entry to edit; a default one creates a new entry
     * \param settings     defaults, rounding and the daily target
     * \param suggestions  what to offer in the drop-down lists
     */
    EntryDialog(const WorkEntry &entry,
                const Settings &settings,
                const EntrySuggestions &suggestions,
                QWidget *parent = nullptr);
    ~EntryDialog() override;

    /*!
     * \brief The saved entry, valid once the dialog was accepted.
     *
     * It is the entry as the user left it, with the configured rounding
     * already applied - exactly what should be written to the database.
     */
    WorkEntry entry() const;

private slots:
    void onDurationTextChanged(const QString &text);
    void onUseTimesToggled(bool enabled);
    void onStartTimeChanged(const QTime &time);
    void onEndTimeChanged(const QTime &time);
    void onDateChanged(const QDate &date);

    void onAddQuarterHour();
    void onAddHalfHour();
    void onAddOneHour();
    void onSetHalfDay();
    void onSetFullDay();
    void onFillTheDay();

    void onIssueKeyActivated(int index);
    void onFillFromRecentClicked();

    void onOpenIssueUrl();
    void onOpenMergeRequest();
    void onOpenTestsheet();

    /*! Validates before closing, so an unusable entry never reaches the database. */
    void onAccepted();

private:
    /*! Puts \a entry into the widgets. */
    void loadEntry(const WorkEntry &entry);

    /*! Reads the widgets back into an entry, without validating it. */
    WorkEntry buildEntry() const;

    /*! Fills the editable drop-downs from the suggestions. */
    void populateChoices();

    /*! Adds \a minutes to the duration field. */
    void addMinutes(int minutes);

    /*! Writes \a minutes into the duration field in the usual shape. */
    void setMinutes(int minutes);

    /*! The duration currently in the field, or 0 when it cannot be read. */
    int currentMinutes(bool *ok = nullptr) const;

    /*! Recomputes the duration from the start and end times. */
    void updateDurationFromTimes();

    /*! Refreshes the grey hint next to the duration field. */
    void updateDurationHint();

    /*! Copies the details of \a source into the issue and link fields. */
    void applyIssueTemplate(const WorkEntry &source);

    /*! Opens \a url in the browser, or explains why it cannot. */
    void openUrl(const QString &url);

    Ui::EntryDialog *m_ui;
    const Settings &m_settings;
    EntrySuggestions m_suggestions;

    /*! The entry the dialog was opened with; keeps the id, source and history. */
    WorkEntry m_originalEntry;

    /*! What entry() hands back, filled in when the dialog is accepted. */
    WorkEntry m_resultEntry;

    /*! Guards the start/end and duration fields from updating each other in a loop. */
    bool m_updatingDuration = false;
};

#endif // WORKCALENDAR_ENTRYDIALOG_H
