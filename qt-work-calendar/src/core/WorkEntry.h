#ifndef WORKCALENDAR_WORKENTRY_H
#define WORKCALENDAR_WORKENTRY_H

#include "core/Enums.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>
#include <QVariant>
#include <QVector>

/*!
 * \brief One logged piece of work: a duration on a day, plus its context.
 *
 * A plain value type on purpose - it is copied into models, dialogs, exporters
 * and back without ownership questions. Everything that interprets an entry
 * (the table, the CSV writer, the Excel report) reads it through field() so a
 * new column only has to be added in one place: the table in WorkEntry.cpp.
 */
struct WorkEntry
{
    /*!
     * \brief Every attribute an entry can show as a column.
     *
     * The order is the default column order of the entry table. Values are
     * never persisted as numbers, so rows may be inserted here freely; the
     * stored column identity is the token in WorkEntry::fieldToken().
     */
    enum Field {
        FieldDate,
        FieldWeekday,
        FieldStartTime,
        FieldEndTime,
        FieldDuration,
        FieldHours,
        FieldIssueKey,
        FieldSummary,
        FieldProject,
        FieldIssueType,
        FieldActivity,
        FieldStatus,
        FieldPriority,
        FieldEpic,
        FieldSprint,
        FieldComponent,
        FieldFixVersion,
        FieldBranch,
        FieldMergeRequest,
        FieldTestsheet,
        FieldTestsheetUrl,
        FieldIssueUrl,
        FieldTags,
        FieldLocation,
        FieldBillable,
        FieldDescription,
        FieldSource,
        FieldExternalId,
        FieldCreatedAt,
        FieldUpdatedAt,
        FieldId,

        FieldCount //!< always last; the number of columns
    };

    int id = -1;                  //!< -1 until the row has been inserted

    QDate date;
    QTime startTime;              //!< optional; null when only a duration was given
    QTime endTime;                //!< optional
    int minutes = 0;              //!< the logged duration, always in whole minutes

    QString issueKey;             //!< "ABC-123"
    QString summary;              //!< the issue title
    QString description;          //!< free notes about what was actually done
    QString project;              //!< empty means "derive from the issue key"
    QString issueType;            //!< Story, Bug, Task, ... kept free-form
    QString epic;
    QString sprint;
    QString component;
    QString fixVersion;
    QString branch;
    QString mergeRequest;         //!< URL of the merge/pull request
    QString testsheetName;
    QString testsheetUrl;
    QString issueUrl;             //!< browse URL of the issue
    QString tags;                 //!< comma separated, free-form

    Activity activity = Activity::Development;
    EntryStatus status = EntryStatus::InProgress;
    Priority priority = Priority::Unset;
    WorkLocation location = WorkLocation::Unset;
    bool billable = true;

    EntrySource source = EntrySource::Manual;
    QString externalId;           //!< Jira worklog id, so a re-import updates in place

    QDateTime createdAt;
    QDateTime updatedAt;

    /*! The project code, falling back to the prefix of the issue key. */
    QString effectiveProject() const;

    /*! "ABC-123 - Fix the parser", or the activity name when there is no issue. */
    QString title() const;

    /*!
     * \brief Checks the entry can be stored.
     * \param[out] error  a sentence naming the problem, when one is found
     */
    bool isValid(QString *error = nullptr) const;

    /*! The value of \a field, ready to be displayed, sorted or exported. */
    QVariant field(Field field) const;

    /*! Writes \a value into \a field, converting from the display shape. */
    void setField(Field field, const QVariant &value);

    /*! The column header shown in the table and written to CSV/Excel. */
    static QString fieldHeader(Field field);

    /*! A stable machine-readable name, used to remember column layouts. */
    static QString fieldToken(Field field);

    /*! The inverse of fieldToken(); returns FieldCount for unknown tokens. */
    static Field fieldFromToken(const QString &token);

    /*! Suggested column width in characters, used by the Excel export. */
    static int fieldWidthHint(Field field);

    /*! True for fields the user cannot edit directly (derived or bookkeeping). */
    static bool fieldIsReadOnly(Field field);

    /*! The columns the entry table shows the first time it is opened. */
    static QVector<Field> defaultVisibleFields();

    /*! Every field, in declaration order. */
    static QVector<Field> allFields();
};

#endif // WORKCALENDAR_WORKENTRY_H
