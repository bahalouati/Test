#ifndef WORKCALENDAR_ENUMS_H
#define WORKCALENDAR_ENUMS_H

#include <QString>
#include <QStringList>

/*!
 * \file Enums.h
 * \brief The small closed vocabularies used by a work entry.
 *
 * Every enum here follows the same three-function pattern:
 *
 *   xxxToString(value)      - the token stored in the database and exports
 *   xxxFromString(text)     - the inverse; unknown text falls back to a default
 *   xxxDisplayName(value)   - what the user sees (may contain spaces/accents)
 *   allXxx()                - every value, in the order combo boxes show them
 *
 * Storing a readable token rather than an integer keeps the SQLite file and the
 * CSV/Excel exports understandable without the application.
 */

/*! What kind of work an entry represents. */
enum class Activity {
    Development,
    BugFix,
    CodeReview,
    Testing,
    Design,
    Meeting,
    Documentation,
    Support,
    Deployment,
    Research,
    Learning,
    Admin,
    Other
};

/*! Progress of the issue at the moment the work was logged. */
enum class EntryStatus {
    NotStarted,
    InProgress,
    InReview,
    Blocked,
    Done
};

/*! Issue importance, mirroring the usual Jira priorities. */
enum class Priority {
    Unset,
    Lowest,
    Low,
    Medium,
    High,
    Highest
};

/*! Where the work happened - useful for hybrid-office reporting. */
enum class WorkLocation {
    Unset,
    Office,
    Home,
    Customer,
    Travel
};

/*! The nature of a calendar day, which drives its target and its colour. */
enum class DayType {
    Workday,
    HalfDay,
    Holiday,
    Vacation,
    SickLeave,
    Training,
    OnCall,
    NonWorking
};

/*! Where an entry came from. Imports use this to stay recognisable. */
enum class EntrySource {
    Manual,
    Timer,
    Csv,
    Jira
};

QString activityToString(Activity value);
Activity activityFromString(const QString &text);
QString activityDisplayName(Activity value);
QVector<Activity> allActivities();

QString entryStatusToString(EntryStatus value);
EntryStatus entryStatusFromString(const QString &text);
QString entryStatusDisplayName(EntryStatus value);
QVector<EntryStatus> allEntryStatuses();

QString priorityToString(Priority value);
Priority priorityFromString(const QString &text);
QString priorityDisplayName(Priority value);
QVector<Priority> allPriorities();

QString workLocationToString(WorkLocation value);
WorkLocation workLocationFromString(const QString &text);
QString workLocationDisplayName(WorkLocation value);
QVector<WorkLocation> allWorkLocations();

QString dayTypeToString(DayType value);
DayType dayTypeFromString(const QString &text);
QString dayTypeDisplayName(DayType value);
QVector<DayType> allDayTypes();

/*!
 * \brief True when a day of this type is expected to contain work.
 *
 * A holiday with no hours logged must not be painted red, and it must not count
 * against the monthly balance.
 */
bool dayTypeExpectsWork(DayType value);

QString entrySourceToString(EntrySource value);
EntrySource entrySourceFromString(const QString &text);
QString entrySourceDisplayName(EntrySource value);

#endif // WORKCALENDAR_ENUMS_H
