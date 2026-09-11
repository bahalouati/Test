#include "core/Settings.h"
#include "data/Statistics.h"

#include <QStandardPaths>
#include <QtTest>

/*!
 * \brief Checks the arithmetic behind the colours, the balance and the reports.
 *
 * The functions under test take their data as arguments, so no database and no
 * clock is involved: "today" is passed in, which makes every expectation here
 * stable forever.
 */
class TestStatistics : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void targetsFollowWorkingDaysAndDayTypes();
    void summariesGroupEntriesPerDay();
    void statesColourDaysCorrectly();
    void absencesNeitherCountNorComplain();
    void totalsAddUpAndTrackStreaks();
    void breakdownsSortAndShare();

private:
    /*! An entry on \a date lasting \a minutes, with just enough to be valid. */
    static WorkEntry entryOn(const QDate &date, int minutes,
                             const QString &issueKey = QStringLiteral("PLAT-1"));

    Settings *m_settings = nullptr;
};

void TestStatistics::initTestCase()
{
    // Keep the test out of the developer's real configuration file.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("WorkCalendarTests"));
    QCoreApplication::setApplicationName(QStringLiteral("StatisticsTest"));

    m_settings = new Settings;
    m_settings->setDailyTargetMinutes(480);
    m_settings->setWarningThresholdMinutes(360);
    m_settings->setWorkingDays(QSet<int>{ Qt::Monday, Qt::Tuesday, Qt::Wednesday,
                                          Qt::Thursday, Qt::Friday });
}

WorkEntry TestStatistics::entryOn(const QDate &date, int minutes, const QString &issueKey)
{
    WorkEntry entry;
    entry.date = date;
    entry.minutes = minutes;
    entry.issueKey = issueKey;
    entry.summary = QStringLiteral("Work");
    return entry;
}

void TestStatistics::targetsFollowWorkingDaysAndDayTypes()
{
    const QDate monday(2026, 9, 7);
    const QDate saturday(2026, 9, 12);

    DayMeta plain;
    QCOMPARE(m_settings->targetMinutesFor(monday, plain), 480);
    QCOMPARE(m_settings->targetMinutesFor(saturday, plain), 0);

    DayMeta halfDay;
    halfDay.type = DayType::HalfDay;
    QCOMPARE(m_settings->targetMinutesFor(monday, halfDay), 240);

    DayMeta holiday;
    holiday.type = DayType::Holiday;
    QCOMPARE(m_settings->targetMinutesFor(monday, holiday), 0);

    // An explicit override wins over everything else, even on a weekend.
    DayMeta overridden;
    overridden.type = DayType::Holiday;
    overridden.targetMinutesOverride = 120;
    QCOMPARE(m_settings->targetMinutesFor(saturday, overridden), 120);
}

void TestStatistics::summariesGroupEntriesPerDay()
{
    const QDate from(2026, 9, 7);   // Monday
    const QDate to(2026, 9, 11);    // Friday

    QVector<WorkEntry> entries;
    entries << entryOn(from, 180) << entryOn(from, 120, QStringLiteral("PLAT-2"));
    WorkEntry unbilled = entryOn(from.addDays(1), 60);
    unbilled.billable = false;
    entries << unbilled;
    entries << entryOn(QDate(2026, 8, 1), 480); // outside the range, must be ignored

    const QVector<DaySummary> days =
        Statistics::buildDaySummaries(from, to, entries, QHash<QDate, DayMeta>(), *m_settings);

    QCOMPARE(days.size(), 5);
    QCOMPARE(days.first().date, from);
    QCOMPARE(days.first().totalMinutes, 300);
    QCOMPARE(days.first().billableMinutes, 300);
    QCOMPARE(days.first().entryCount, 2);
    QCOMPARE(days.first().lines.size(), 2);
    QCOMPARE(days.first().targetMinutes, 480);
    QCOMPARE(days.first().balanceMinutes(), -180);

    QCOMPARE(days.at(1).totalMinutes, 60);
    QCOMPARE(days.at(1).billableMinutes, 0);

    QCOMPARE(days.last().totalMinutes, 0);
}

void TestStatistics::statesColourDaysCorrectly()
{
    const QDate today(2026, 9, 11);
    const int threshold = 360;

    DaySummary day;
    day.date = QDate(2026, 9, 10);
    day.targetMinutes = 480;

    day.totalMinutes = 480;
    QCOMPARE(day.state(today, threshold), DaySummary::Complete);

    day.totalMinutes = 540;
    QCOMPARE(day.state(today, threshold), DaySummary::Complete);

    day.totalMinutes = 400;
    QCOMPARE(day.state(today, threshold), DaySummary::Partial);

    day.totalMinutes = 120;
    QCOMPARE(day.state(today, threshold), DaySummary::Low);

    day.totalMinutes = 0;
    QCOMPARE(day.state(today, threshold), DaySummary::Empty);

    // The same empty day, but still ahead: nothing is due yet.
    day.date = QDate(2026, 9, 30);
    QCOMPARE(day.state(today, threshold), DaySummary::Future);

    // A weekend with no target and no work stays neutral; work on it shows.
    DaySummary weekend;
    weekend.date = QDate(2026, 9, 12);
    weekend.targetMinutes = 0;
    QCOMPARE(weekend.state(today, threshold), DaySummary::NonWorking);
    weekend.totalMinutes = 120;
    QCOMPARE(weekend.state(today, threshold), DaySummary::Complete);
}

void TestStatistics::absencesNeitherCountNorComplain()
{
    const QDate from(2026, 9, 7);
    const QDate to(2026, 9, 11);
    const QDate today(2026, 9, 30); // the whole week is in the past

    QHash<QDate, DayMeta> dayMeta;
    DayMeta vacation;
    vacation.date = QDate(2026, 9, 9);
    vacation.type = DayType::Vacation;
    dayMeta.insert(vacation.date, vacation);

    const QVector<DaySummary> days =
        Statistics::buildDaySummaries(from, to, QVector<WorkEntry>(), dayMeta, *m_settings);

    const DaySummary vacationDay = days.at(2);
    QCOMPARE(vacationDay.targetMinutes, 0);
    QCOMPARE(vacationDay.state(today, 360), DaySummary::Absence);

    const Statistics::PeriodTotals totals = Statistics::totals(days, today);
    QCOMPARE(totals.absenceDays, 1);
    QCOMPARE(totals.expectedDays, 4);        // the vacation day expects nothing
    QCOMPARE(totals.targetMinutes, 4 * 480); // and does not inflate the target
    QCOMPARE(totals.shortDays, 4);
}

void TestStatistics::totalsAddUpAndTrackStreaks()
{
    const QDate from(2026, 9, 7);   // Monday
    const QDate to(2026, 9, 18);    // the Friday of the following week
    const QDate today(2026, 9, 18);

    QVector<WorkEntry> entries;
    // Week one: four full days, then a short Friday.
    entries << entryOn(QDate(2026, 9, 7), 480);
    entries << entryOn(QDate(2026, 9, 8), 480);
    entries << entryOn(QDate(2026, 9, 9), 480);
    entries << entryOn(QDate(2026, 9, 10), 480);
    entries << entryOn(QDate(2026, 9, 11), 120);
    // Week two: two full days.
    entries << entryOn(QDate(2026, 9, 14), 480);
    entries << entryOn(QDate(2026, 9, 15), 540);

    const QVector<DaySummary> days =
        Statistics::buildDaySummaries(from, to, entries, QHash<QDate, DayMeta>(), *m_settings);
    const Statistics::PeriodTotals totals = Statistics::totals(days, today);

    QCOMPARE(totals.totalMinutes, 3060);
    QCOMPARE(totals.targetMinutes, 10 * 480);   // ten working days in the range
    QCOMPARE(totals.balanceMinutes(), 3060 - 4800);
    QCOMPARE(totals.daysWithWork, 7);
    QCOMPARE(totals.expectedDays, 10);
    QCOMPARE(totals.completeDays, 6);
    QCOMPARE(totals.longestCompleteStreak, 4);
    QCOMPARE(totals.averageMinutesPerWorkedDay(), 3060 / 7);
    QCOMPARE(totals.entryCount, 7);
}

void TestStatistics::breakdownsSortAndShare()
{
    QVector<WorkEntry> entries;

    WorkEntry development = entryOn(QDate(2026, 9, 7), 300);
    development.project = QStringLiteral("PLAT");
    development.activity = Activity::Development;
    entries << development;

    WorkEntry meeting = entryOn(QDate(2026, 9, 7), 60);
    meeting.project = QStringLiteral("PLAT");
    meeting.activity = Activity::Meeting;
    meeting.billable = false;
    entries << meeting;

    WorkEntry other = entryOn(QDate(2026, 9, 8), 120);
    other.project = QStringLiteral("SUP");
    other.activity = Activity::Support;
    entries << other;

    const QVector<Statistics::BreakdownRow> byProject =
        Statistics::breakdownBy(entries, WorkEntry::FieldProject);
    QCOMPARE(byProject.size(), 2);
    QCOMPARE(byProject.first().key, QStringLiteral("PLAT")); // biggest first
    QCOMPARE(byProject.first().minutes, 360);
    QCOMPARE(byProject.first().billableMinutes, 300);
    QCOMPARE(byProject.first().entryCount, 2);
    QVERIFY(qAbs(byProject.first().share - 360.0 / 480.0) < 0.0001);

    const QVector<Statistics::BreakdownRow> byActivity =
        Statistics::breakdownBy(entries, WorkEntry::FieldActivity);
    QCOMPARE(byActivity.size(), 3);
    QCOMPARE(byActivity.first().key, QStringLiteral("Development"));

    // A field nobody would group by yields nothing rather than nonsense.
    QVERIFY(Statistics::breakdownBy(entries, WorkEntry::FieldDescription).isEmpty());
}

QTEST_GUILESS_MAIN(TestStatistics)
#include "tst_statistics.moc"
