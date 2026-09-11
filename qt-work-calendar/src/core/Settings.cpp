#include "core/Settings.h"

#include "core/DaySummary.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace {

// Every key in one place, so a typo is a compile error rather than a setting
// that silently resets itself on the next start.
const QString kDailyTarget         = QStringLiteral("targets/dailyMinutes");
const QString kWarningThreshold    = QStringLiteral("targets/warningMinutes");
const QString kWorkingDays         = QStringLiteral("targets/workingDays");

const QString kDefaultActivity     = QStringLiteral("defaults/activity");
const QString kDefaultLocation     = QStringLiteral("defaults/location");
const QString kDefaultBillable     = QStringLiteral("defaults/billable");
const QString kRoundingIncrement   = QStringLiteral("defaults/roundingMinutes");
const QString kDefaultStartTime    = QStringLiteral("defaults/startTime");

const QString kDurationFormat      = QStringLiteral("appearance/durationFormat");
const QString kShowWeekends        = QStringLiteral("appearance/showWeekends");
const QString kMaxLinesPerDay      = QStringLiteral("appearance/maxLinesPerDay");
const QString kDayColorPrefix      = QStringLiteral("appearance/dayColor/");

const QString kJiraBaseUrl         = QStringLiteral("jira/baseUrl");
const QString kJiraUsername        = QStringLiteral("jira/username");
const QString kJiraToken           = QStringLiteral("jira/token");
const QString kJiraSprintField     = QStringLiteral("jira/sprintField");
const QString kJiraTestsheetWord   = QStringLiteral("jira/testsheetKeyword");
const QString kJiraMergeMarker     = QStringLiteral("jira/mergeRequestMarker");

const QString kUserName            = QStringLiteral("general/userName");
const QString kLastExportDir       = QStringLiteral("general/lastExportDirectory");
const QString kVisibleFields       = QStringLiteral("general/visibleEntryFields");
const QString kWindowGeometry      = QStringLiteral("window/geometry");
const QString kWindowState         = QStringLiteral("window/state");
const QString kHeaderState         = QStringLiteral("window/entryHeaderState");

const QString kTimerStartedAt      = QStringLiteral("timer/startedAt");
const QString kTimerAccumulated    = QStringLiteral("timer/accumulatedSeconds");
const QString kTimerIssueKey       = QStringLiteral("timer/issueKey");
const QString kTimerSummary        = QStringLiteral("timer/summary");
const QString kTimerActivity       = QStringLiteral("timer/activity");

const int kDefaultDailyTarget = 8 * 60;
const int kDefaultWarning     = 6 * 60;

} // namespace

Settings::Settings()
    : m_settings(QSettings::IniFormat,
                 QSettings::UserScope,
                 QCoreApplication::organizationName(),
                 QCoreApplication::applicationName())
{
}

void Settings::sync()
{
    m_settings.sync();
}

int Settings::readInt(const QString &key, int fallback) const
{
    bool ok = false;
    const int value = m_settings.value(key, fallback).toInt(&ok);
    return ok ? value : fallback;
}

QString Settings::readString(const QString &key, const QString &fallback) const
{
    return m_settings.value(key, fallback).toString();
}

bool Settings::readBool(const QString &key, bool fallback) const
{
    return m_settings.value(key, fallback).toBool();
}

void Settings::write(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
}

// ----------------------------------------------------------------- targets

int Settings::dailyTargetMinutes() const
{
    return qBound(1, readInt(kDailyTarget, kDefaultDailyTarget), 24 * 60);
}

void Settings::setDailyTargetMinutes(int minutes)
{
    write(kDailyTarget, qBound(1, minutes, 24 * 60));
}

int Settings::warningThresholdMinutes() const
{
    return qBound(0, readInt(kWarningThreshold, kDefaultWarning), dailyTargetMinutes());
}

void Settings::setWarningThresholdMinutes(int minutes)
{
    write(kWarningThreshold, qMax(0, minutes));
}

QSet<int> Settings::workingDays() const
{
    const QVariant stored = m_settings.value(kWorkingDays);
    if (!stored.isValid()) {
        // Monday to Friday.
        return QSet<int>{ Qt::Monday, Qt::Tuesday, Qt::Wednesday, Qt::Thursday, Qt::Friday };
    }

    QSet<int> days;
    const QStringList parts = stored.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int day = part.trimmed().toInt();
        if (day >= Qt::Monday && day <= Qt::Sunday)
            days.insert(day);
    }
    return days;
}

void Settings::setWorkingDays(const QSet<int> &days)
{
    QStringList parts;
    for (int day = Qt::Monday; day <= Qt::Sunday; ++day) {
        if (days.contains(day))
            parts.append(QString::number(day));
    }
    write(kWorkingDays, parts.join(QLatin1Char(',')));
}

bool Settings::isWorkingDay(const QDate &date) const
{
    return date.isValid() && workingDays().contains(date.dayOfWeek());
}

int Settings::targetMinutesFor(const QDate &date, const DayMeta &meta) const
{
    if (meta.targetMinutesOverride >= 0)
        return meta.targetMinutesOverride;

    if (!dayTypeExpectsWork(meta.type))
        return 0;

    if (!isWorkingDay(date))
        return 0;

    if (meta.type == DayType::HalfDay)
        return dailyTargetMinutes() / 2;

    return dailyTargetMinutes();
}

// ---------------------------------------------------------------- defaults

Activity Settings::defaultActivity() const
{
    return activityFromString(readString(kDefaultActivity, activityToString(Activity::Development)));
}

void Settings::setDefaultActivity(Activity activity)
{
    write(kDefaultActivity, activityToString(activity));
}

WorkLocation Settings::defaultLocation() const
{
    return workLocationFromString(readString(kDefaultLocation, workLocationToString(WorkLocation::Unset)));
}

void Settings::setDefaultLocation(WorkLocation location)
{
    write(kDefaultLocation, workLocationToString(location));
}

bool Settings::defaultBillable() const
{
    return readBool(kDefaultBillable, true);
}

void Settings::setDefaultBillable(bool billable)
{
    write(kDefaultBillable, billable);
}

int Settings::roundingIncrementMinutes() const
{
    return qBound(0, readInt(kRoundingIncrement, 0), 120);
}

void Settings::setRoundingIncrementMinutes(int minutes)
{
    write(kRoundingIncrement, qBound(0, minutes, 120));
}

QTime Settings::defaultStartTime() const
{
    const QTime stored = QTime::fromString(readString(kDefaultStartTime, QString()),
                                           QStringLiteral("HH:mm"));
    return stored.isValid() ? stored : QTime(9, 0);
}

void Settings::setDefaultStartTime(const QTime &time)
{
    write(kDefaultStartTime, time.toString(QStringLiteral("HH:mm")));
}

// -------------------------------------------------------------- appearance

Duration::Format Settings::durationFormat() const
{
    const QString stored = readString(kDurationFormat, QStringLiteral("hm"));
    if (stored == QLatin1String("decimal"))
        return Duration::Format::Decimal;
    if (stored == QLatin1String("clock"))
        return Duration::Format::Clock;
    return Duration::Format::HoursAndMinutes;
}

void Settings::setDurationFormat(Duration::Format format)
{
    switch (format) {
    case Duration::Format::Decimal:
        write(kDurationFormat, QStringLiteral("decimal"));
        break;
    case Duration::Format::Clock:
        write(kDurationFormat, QStringLiteral("clock"));
        break;
    case Duration::Format::HoursAndMinutes:
        write(kDurationFormat, QStringLiteral("hm"));
        break;
    }
}

bool Settings::showWeekends() const
{
    return readBool(kShowWeekends, false);
}

void Settings::setShowWeekends(bool show)
{
    write(kShowWeekends, show);
}

int Settings::maxLinesPerDay() const
{
    return qBound(0, readInt(kMaxLinesPerDay, 5), 20);
}

void Settings::setMaxLinesPerDay(int lines)
{
    write(kMaxLinesPerDay, qBound(0, lines, 20));
}

QColor Settings::defaultDayColor(int state) const
{
    // The palette of the spreadsheet this replaces: green complete, amber close,
    // red short, grey for days nothing is expected of.
    switch (state) {
    case DaySummary::Complete:   return QColor(0xC6, 0xEF, 0xCE);
    case DaySummary::Partial:    return QColor(0xFF, 0xF2, 0xCC);
    case DaySummary::Low:        return QColor(0xFF, 0xC7, 0xCE);
    case DaySummary::Empty:      return QColor(0xFA, 0xDD, 0xE0);
    case DaySummary::Future:     return QColor(0xF2, 0xF2, 0xF2);
    case DaySummary::Absence:    return QColor(0xDA, 0xE8, 0xFC);
    case DaySummary::NonWorking: return QColor(0xE8, 0xE8, 0xE8);
    default:                     return QColor(Qt::white);
    }
}

QColor Settings::dayColor(int state) const
{
    const QString key = kDayColorPrefix + QString::number(state);
    const QString stored = readString(key, QString());
    const QColor color(stored);
    return color.isValid() ? color : defaultDayColor(state);
}

void Settings::setDayColor(int state, const QColor &color)
{
    write(kDayColorPrefix + QString::number(state), color.name());
}

// -------------------------------------------------------------------- Jira

QString Settings::jiraBaseUrl() const { return readString(kJiraBaseUrl, QString()); }
void Settings::setJiraBaseUrl(const QString &url) { write(kJiraBaseUrl, url.trimmed()); }

QString Settings::jiraUsername() const { return readString(kJiraUsername, QString()); }
void Settings::setJiraUsername(const QString &username) { write(kJiraUsername, username.trimmed()); }

QString Settings::jiraToken() const { return readString(kJiraToken, QString()); }
void Settings::setJiraToken(const QString &token) { write(kJiraToken, token); }

QString Settings::jiraSprintField() const
{
    return readString(kJiraSprintField, QStringLiteral("customfield_10005"));
}

void Settings::setJiraSprintField(const QString &field) { write(kJiraSprintField, field.trimmed()); }

QString Settings::jiraTestsheetKeyword() const
{
    return readString(kJiraTestsheetWord, QStringLiteral("testsheet"));
}

void Settings::setJiraTestsheetKeyword(const QString &keyword)
{
    write(kJiraTestsheetWord, keyword.trimmed());
}

QString Settings::jiraMergeRequestMarker() const
{
    return readString(kJiraMergeMarker, QStringLiteral("/merge_requests/"));
}

void Settings::setJiraMergeRequestMarker(const QString &marker)
{
    write(kJiraMergeMarker, marker.trimmed());
}

// ----------------------------------------------------------------- general

QString Settings::userName() const
{
    return readString(kUserName, QString());
}

void Settings::setUserName(const QString &name) { write(kUserName, name.trimmed()); }

QString Settings::lastExportDirectory() const
{
    const QString stored = readString(kLastExportDir, QString());
    if (!stored.isEmpty() && QDir(stored).exists())
        return stored;
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void Settings::setLastExportDirectory(const QString &path) { write(kLastExportDir, path); }

QStringList Settings::visibleEntryFields() const
{
    return m_settings.value(kVisibleFields).toStringList();
}

void Settings::setVisibleEntryFields(const QStringList &tokens) { write(kVisibleFields, tokens); }

QByteArray Settings::mainWindowGeometry() const
{
    return m_settings.value(kWindowGeometry).toByteArray();
}

void Settings::setMainWindowGeometry(const QByteArray &geometry) { write(kWindowGeometry, geometry); }

QByteArray Settings::mainWindowState() const
{
    return m_settings.value(kWindowState).toByteArray();
}

void Settings::setMainWindowState(const QByteArray &state) { write(kWindowState, state); }

QByteArray Settings::entryTableHeaderState() const
{
    return m_settings.value(kHeaderState).toByteArray();
}

void Settings::setEntryTableHeaderState(const QByteArray &state) { write(kHeaderState, state); }

// ----------------------------------------------------------- running timer

QDateTime Settings::timerStartedAt() const
{
    return QDateTime::fromString(readString(kTimerStartedAt, QString()), Qt::ISODate);
}

void Settings::setTimerStartedAt(const QDateTime &startedAt)
{
    write(kTimerStartedAt, startedAt.isValid() ? startedAt.toString(Qt::ISODate) : QString());
}

int Settings::timerAccumulatedSeconds() const
{
    return qMax(0, readInt(kTimerAccumulated, 0));
}

void Settings::setTimerAccumulatedSeconds(int seconds)
{
    write(kTimerAccumulated, qMax(0, seconds));
}

QString Settings::timerIssueKey() const { return readString(kTimerIssueKey, QString()); }
void Settings::setTimerIssueKey(const QString &issueKey) { write(kTimerIssueKey, issueKey); }

QString Settings::timerSummary() const { return readString(kTimerSummary, QString()); }
void Settings::setTimerSummary(const QString &summary) { write(kTimerSummary, summary); }

Activity Settings::timerActivity() const
{
    return activityFromString(readString(kTimerActivity, activityToString(defaultActivity())));
}

void Settings::setTimerActivity(Activity activity)
{
    write(kTimerActivity, activityToString(activity));
}
