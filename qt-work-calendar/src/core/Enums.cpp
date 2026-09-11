#include "Enums.h"

#include <QVector>

namespace {

/*!
 * \brief One row of the translation table used by every enum below.
 *
 * The table is walked linearly. The lists are short (at most a dozen entries)
 * and the lookups happen per painted cell at worst, so a map buys nothing and
 * costs readability.
 */
template <typename Enum>
struct Naming {
    Enum value;
    const char *token;       //!< stored in SQLite, CSV and Excel
    const char *displayName; //!< shown in the interface
};

template <typename Enum, int Size>
QString tokenFor(const Naming<Enum> (&table)[Size], Enum value)
{
    for (int i = 0; i < Size; ++i) {
        if (table[i].value == value)
            return QString::fromLatin1(table[i].token);
    }
    return QString::fromLatin1(table[0].token);
}

template <typename Enum, int Size>
QString displayNameFor(const Naming<Enum> (&table)[Size], Enum value)
{
    for (int i = 0; i < Size; ++i) {
        if (table[i].value == value)
            return QString::fromLatin1(table[i].displayName);
    }
    return QString::fromLatin1(table[0].displayName);
}

/*!
 * \brief Reverse lookup.
 *
 * Both the token and the display name are accepted so that a CSV hand-edited in
 * a spreadsheet ("Code review") still imports. Unknown text yields the first
 * row, which is always the neutral default of its enum.
 */
template <typename Enum, int Size>
Enum valueFor(const Naming<Enum> (&table)[Size], const QString &text)
{
    for (int i = 0; i < Size; ++i) {
        if (text.compare(QString::fromLatin1(table[i].token), Qt::CaseInsensitive) == 0)
            return table[i].value;
    }
    for (int i = 0; i < Size; ++i) {
        if (text.compare(QString::fromLatin1(table[i].displayName), Qt::CaseInsensitive) == 0)
            return table[i].value;
    }
    return table[0].value;
}

template <typename Enum, int Size>
QVector<Enum> valuesOf(const Naming<Enum> (&table)[Size])
{
    QVector<Enum> values;
    values.reserve(Size);
    for (int i = 0; i < Size; ++i)
        values.append(table[i].value);
    return values;
}

const Naming<Activity> activityTable[] = {
    { Activity::Development,   "development",   "Development"   },
    { Activity::BugFix,        "bugfix",        "Bug fix"       },
    { Activity::CodeReview,    "code_review",   "Code review"   },
    { Activity::Testing,       "testing",       "Testing"       },
    { Activity::Design,        "design",        "Design"        },
    { Activity::Meeting,       "meeting",       "Meeting"       },
    { Activity::Documentation, "documentation", "Documentation" },
    { Activity::Support,       "support",       "Support"       },
    { Activity::Deployment,    "deployment",    "Deployment"    },
    { Activity::Research,      "research",      "Research"      },
    { Activity::Learning,      "learning",      "Learning"      },
    { Activity::Admin,         "admin",         "Admin"         },
    { Activity::Other,         "other",         "Other"         }
};

const Naming<EntryStatus> statusTable[] = {
    { EntryStatus::NotStarted, "not_started", "Not started" },
    { EntryStatus::InProgress, "in_progress", "In progress" },
    { EntryStatus::InReview,   "in_review",   "In review"   },
    { EntryStatus::Blocked,    "blocked",     "Blocked"     },
    { EntryStatus::Done,       "done",        "Done"        }
};

const Naming<Priority> priorityTable[] = {
    { Priority::Unset,   "",        "-"       },
    { Priority::Lowest,  "lowest",  "Lowest"  },
    { Priority::Low,     "low",     "Low"     },
    { Priority::Medium,  "medium",  "Medium"  },
    { Priority::High,    "high",    "High"    },
    { Priority::Highest, "highest", "Highest" }
};

const Naming<WorkLocation> locationTable[] = {
    { WorkLocation::Unset,    "",         "-"        },
    { WorkLocation::Office,   "office",   "Office"   },
    { WorkLocation::Home,     "home",     "Home"     },
    { WorkLocation::Customer, "customer", "Customer" },
    { WorkLocation::Travel,   "travel",   "Travel"   }
};

const Naming<DayType> dayTypeTable[] = {
    { DayType::Workday,    "workday",     "Workday"     },
    { DayType::HalfDay,    "half_day",    "Half day"    },
    { DayType::Holiday,    "holiday",     "Holiday"     },
    { DayType::Vacation,   "vacation",    "Vacation"    },
    { DayType::SickLeave,  "sick_leave",  "Sick leave"  },
    { DayType::Training,   "training",    "Training"    },
    { DayType::OnCall,     "on_call",     "On call"     },
    { DayType::NonWorking, "non_working", "Non-working" }
};

const Naming<EntrySource> sourceTable[] = {
    { EntrySource::Manual, "manual", "Manual" },
    { EntrySource::Timer,  "timer",  "Timer"  },
    { EntrySource::Csv,    "csv",    "CSV"    },
    { EntrySource::Jira,   "jira",   "Jira"   }
};

} // namespace

QString activityToString(Activity value) { return tokenFor(activityTable, value); }
Activity activityFromString(const QString &text) { return valueFor(activityTable, text); }
QString activityDisplayName(Activity value) { return displayNameFor(activityTable, value); }
QVector<Activity> allActivities() { return valuesOf(activityTable); }

QString entryStatusToString(EntryStatus value) { return tokenFor(statusTable, value); }
EntryStatus entryStatusFromString(const QString &text) { return valueFor(statusTable, text); }
QString entryStatusDisplayName(EntryStatus value) { return displayNameFor(statusTable, value); }
QVector<EntryStatus> allEntryStatuses() { return valuesOf(statusTable); }

QString priorityToString(Priority value) { return tokenFor(priorityTable, value); }
Priority priorityFromString(const QString &text) { return valueFor(priorityTable, text); }
QString priorityDisplayName(Priority value) { return displayNameFor(priorityTable, value); }
QVector<Priority> allPriorities() { return valuesOf(priorityTable); }

QString workLocationToString(WorkLocation value) { return tokenFor(locationTable, value); }
WorkLocation workLocationFromString(const QString &text) { return valueFor(locationTable, text); }
QString workLocationDisplayName(WorkLocation value) { return displayNameFor(locationTable, value); }
QVector<WorkLocation> allWorkLocations() { return valuesOf(locationTable); }

QString dayTypeToString(DayType value) { return tokenFor(dayTypeTable, value); }
DayType dayTypeFromString(const QString &text) { return valueFor(dayTypeTable, text); }
QString dayTypeDisplayName(DayType value) { return displayNameFor(dayTypeTable, value); }
QVector<DayType> allDayTypes() { return valuesOf(dayTypeTable); }

bool dayTypeExpectsWork(DayType value)
{
    switch (value) {
    case DayType::Workday:
    case DayType::HalfDay:
    case DayType::Training:
    case DayType::OnCall:
        return true;
    case DayType::Holiday:
    case DayType::Vacation:
    case DayType::SickLeave:
    case DayType::NonWorking:
        return false;
    }
    return true;
}

QString entrySourceToString(EntrySource value) { return tokenFor(sourceTable, value); }
EntrySource entrySourceFromString(const QString &text) { return valueFor(sourceTable, text); }
QString entrySourceDisplayName(EntrySource value) { return displayNameFor(sourceTable, value); }
