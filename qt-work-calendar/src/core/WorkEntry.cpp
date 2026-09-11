#include "core/WorkEntry.h"

#include "core/Duration.h"

#include <QLocale>

namespace {

/*! Column metadata. One row per WorkEntry::Field, in enum order. */
struct FieldInfo {
    WorkEntry::Field field;
    const char *token;   //!< stored in settings when remembering column layouts
    const char *header;  //!< shown to the user
    int widthHint;       //!< characters, used for Excel column widths
    bool readOnly;       //!< derived or bookkeeping, not editable in the dialog
    bool visibleByDefault;
};

const FieldInfo fieldTable[] = {
    { WorkEntry::FieldDate,         "date",         "Date",          12, false, true  },
    { WorkEntry::FieldWeekday,      "weekday",      "Day",           10, true,  false },
    { WorkEntry::FieldStartTime,    "start",        "Start",          8, false, true  },
    { WorkEntry::FieldEndTime,      "end",          "End",            8, false, true  },
    { WorkEntry::FieldDuration,     "duration",     "Duration",      10, false, true  },
    { WorkEntry::FieldHours,        "hours",        "Hours",          8, true,  false },
    { WorkEntry::FieldIssueKey,     "issue",        "Issue",         14, false, true  },
    { WorkEntry::FieldSummary,      "summary",      "Summary",       46, false, true  },
    { WorkEntry::FieldProject,      "project",      "Project",       14, false, false },
    { WorkEntry::FieldIssueType,    "issue_type",   "Type",          12, false, false },
    { WorkEntry::FieldActivity,     "activity",     "Activity",      16, false, true  },
    { WorkEntry::FieldStatus,       "status",       "Status",        13, false, true  },
    { WorkEntry::FieldPriority,     "priority",     "Priority",      10, false, false },
    { WorkEntry::FieldEpic,         "epic",         "Epic",          18, false, false },
    { WorkEntry::FieldSprint,       "sprint",       "Sprint",        22, false, true  },
    { WorkEntry::FieldComponent,    "component",    "Component",     18, false, false },
    { WorkEntry::FieldFixVersion,   "fix_version",  "Fix version",   18, false, true  },
    { WorkEntry::FieldBranch,       "branch",       "Branch",        26, false, false },
    { WorkEntry::FieldMergeRequest, "merge_request","MR link",       34, false, true  },
    { WorkEntry::FieldTestsheet,    "testsheet",    "Testsheet",     26, false, true  },
    { WorkEntry::FieldTestsheetUrl, "testsheet_url","Testsheet URL", 34, false, false },
    { WorkEntry::FieldIssueUrl,     "issue_url",    "Issue URL",     34, false, false },
    { WorkEntry::FieldTags,         "tags",         "Tags",          20, false, false },
    { WorkEntry::FieldLocation,     "location",     "Location",      12, false, false },
    { WorkEntry::FieldBillable,     "billable",     "Billable",      10, false, false },
    { WorkEntry::FieldDescription,  "description",  "Notes",         50, false, false },
    { WorkEntry::FieldSource,       "source",       "Source",        10, true,  false },
    { WorkEntry::FieldExternalId,   "external_id",  "External id",   16, true,  false },
    { WorkEntry::FieldCreatedAt,    "created_at",   "Created",       18, true,  false },
    { WorkEntry::FieldUpdatedAt,    "updated_at",   "Updated",       18, true,  false },
    { WorkEntry::FieldId,           "id",           "#",              7, true,  false }
};

static_assert(sizeof(fieldTable) / sizeof(fieldTable[0]) == WorkEntry::FieldCount,
              "fieldTable must describe every WorkEntry::Field");

const FieldInfo &infoFor(WorkEntry::Field field)
{
    return fieldTable[static_cast<int>(field)];
}

/*! "" for a null time, "09:30" otherwise. */
QString formatTime(const QTime &time)
{
    return time.isValid() ? time.toString(QStringLiteral("HH:mm")) : QString();
}

} // namespace

QString WorkEntry::effectiveProject() const
{
    if (!project.trimmed().isEmpty())
        return project.trimmed();

    const int dash = issueKey.indexOf(QLatin1Char('-'));
    if (dash > 0)
        return issueKey.left(dash).toUpper();
    return QString();
}

QString WorkEntry::title() const
{
    const QString trimmedSummary = summary.trimmed();
    if (!issueKey.trimmed().isEmpty() && !trimmedSummary.isEmpty())
        return QStringLiteral("%1 - %2").arg(issueKey.trimmed(), trimmedSummary);
    if (!issueKey.trimmed().isEmpty())
        return issueKey.trimmed();
    if (!trimmedSummary.isEmpty())
        return trimmedSummary;
    return activityDisplayName(activity);
}

bool WorkEntry::isValid(QString *error) const
{
    if (!date.isValid()) {
        if (error)
            *error = QStringLiteral("The entry needs a date.");
        return false;
    }
    if (minutes <= 0) {
        if (error)
            *error = QStringLiteral("The duration must be greater than zero.");
        return false;
    }
    if (minutes > 24 * 60) {
        if (error)
            *error = QStringLiteral("A single entry cannot be longer than 24 hours.");
        return false;
    }
    if (issueKey.trimmed().isEmpty() && summary.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Give the entry an issue key or a summary, "
                                    "so it can be recognised later.");
        return false;
    }
    if (startTime.isValid() && endTime.isValid() && endTime < startTime) {
        if (error)
            *error = QStringLiteral("The end time is before the start time.");
        return false;
    }
    return true;
}

QVariant WorkEntry::field(Field requested) const
{
    switch (requested) {
    case FieldDate:         return date;
    case FieldWeekday:      return date.isValid() ? QLocale().dayName(date.dayOfWeek(), QLocale::ShortFormat)
                                                  : QString();
    case FieldStartTime:    return formatTime(startTime);
    case FieldEndTime:      return formatTime(endTime);
    case FieldDuration:     return Duration::format(minutes);
    case FieldHours:        return Duration::toHours(minutes);
    case FieldIssueKey:     return issueKey;
    case FieldSummary:      return summary;
    case FieldProject:      return effectiveProject();
    case FieldIssueType:    return issueType;
    case FieldActivity:     return activityDisplayName(activity);
    case FieldStatus:       return entryStatusDisplayName(status);
    case FieldPriority:     return priorityDisplayName(priority);
    case FieldEpic:         return epic;
    case FieldSprint:       return sprint;
    case FieldComponent:    return component;
    case FieldFixVersion:   return fixVersion;
    case FieldBranch:       return branch;
    case FieldMergeRequest: return mergeRequest;
    case FieldTestsheet:    return testsheetName;
    case FieldTestsheetUrl: return testsheetUrl;
    case FieldIssueUrl:     return issueUrl;
    case FieldTags:         return tags;
    case FieldLocation:     return workLocationDisplayName(location);
    case FieldBillable:     return billable;
    case FieldDescription:  return description;
    case FieldSource:       return entrySourceDisplayName(source);
    case FieldExternalId:   return externalId;
    case FieldCreatedAt:    return createdAt;
    case FieldUpdatedAt:    return updatedAt;
    case FieldId:           return id;
    case FieldCount:        break;
    }
    return QVariant();
}

void WorkEntry::setField(Field requested, const QVariant &value)
{
    const QString text = value.toString().trimmed();

    switch (requested) {
    case FieldDate:
        date = value.canConvert<QDate>() && value.toDate().isValid()
                   ? value.toDate()
                   : QDate::fromString(text, Qt::ISODate);
        break;
    case FieldStartTime:
        startTime = QTime::fromString(text, QStringLiteral("HH:mm"));
        break;
    case FieldEndTime:
        endTime = QTime::fromString(text, QStringLiteral("HH:mm"));
        break;
    case FieldDuration:
        minutes = Duration::parseMinutes(text);
        break;
    case FieldHours:
        minutes = Duration::fromHours(value.toDouble());
        break;
    case FieldIssueKey:     issueKey = text; break;
    case FieldSummary:      summary = value.toString(); break;
    case FieldProject:      project = text; break;
    case FieldIssueType:    issueType = text; break;
    case FieldActivity:     activity = activityFromString(text); break;
    case FieldStatus:       status = entryStatusFromString(text); break;
    case FieldPriority:     priority = priorityFromString(text); break;
    case FieldEpic:         epic = text; break;
    case FieldSprint:       sprint = text; break;
    case FieldComponent:    component = text; break;
    case FieldFixVersion:   fixVersion = text; break;
    case FieldBranch:       branch = text; break;
    case FieldMergeRequest: mergeRequest = text; break;
    case FieldTestsheet:    testsheetName = text; break;
    case FieldTestsheetUrl: testsheetUrl = text; break;
    case FieldIssueUrl:     issueUrl = text; break;
    case FieldTags:         tags = text; break;
    case FieldLocation:     location = workLocationFromString(text); break;
    case FieldBillable:
        billable = text.compare(QStringLiteral("no"), Qt::CaseInsensitive) != 0
                   && text.compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0
                   && text != QStringLiteral("0");
        break;
    case FieldDescription:  description = value.toString(); break;
    case FieldSource:       source = entrySourceFromString(text); break;
    case FieldExternalId:   externalId = text; break;
    case FieldCreatedAt:    createdAt = QDateTime::fromString(text, Qt::ISODate); break;
    case FieldUpdatedAt:    updatedAt = QDateTime::fromString(text, Qt::ISODate); break;
    case FieldId:           id = value.toInt(); break;
    case FieldWeekday:
    case FieldCount:
        break; // derived, nothing to store
    }
}

QString WorkEntry::fieldHeader(Field field)
{
    return QString::fromLatin1(infoFor(field).header);
}

QString WorkEntry::fieldToken(Field field)
{
    return QString::fromLatin1(infoFor(field).token);
}

WorkEntry::Field WorkEntry::fieldFromToken(const QString &token)
{
    for (int i = 0; i < FieldCount; ++i) {
        if (token.compare(QString::fromLatin1(fieldTable[i].token), Qt::CaseInsensitive) == 0)
            return fieldTable[i].field;
    }
    return FieldCount;
}

int WorkEntry::fieldWidthHint(Field field)
{
    return infoFor(field).widthHint;
}

bool WorkEntry::fieldIsReadOnly(Field field)
{
    return infoFor(field).readOnly;
}

QVector<WorkEntry::Field> WorkEntry::defaultVisibleFields()
{
    QVector<Field> fields;
    for (int i = 0; i < FieldCount; ++i) {
        if (fieldTable[i].visibleByDefault)
            fields.append(fieldTable[i].field);
    }
    return fields;
}

QVector<WorkEntry::Field> WorkEntry::allFields()
{
    QVector<Field> fields;
    fields.reserve(FieldCount);
    for (int i = 0; i < FieldCount; ++i)
        fields.append(fieldTable[i].field);
    return fields;
}
