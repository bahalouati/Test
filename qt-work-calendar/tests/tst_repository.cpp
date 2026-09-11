#include "data/DayMetaRepository.h"
#include "data/Database.h"
#include "data/WorkEntryRepository.h"

#include <QTemporaryDir>
#include <QtTest>

/*!
 * \brief Exercises storage against a real, throwaway SQLite database.
 *
 * Each test gets its own in-memory database, so the tests cannot influence one
 * another and no file is left behind.
 */
class TestRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void opensAndCreatesSchema();
    void opensAFileDatabaseAndKeepsIt();
    void addsAndReadsBackEveryField();
    void rejectsInvalidEntries();
    void updatesInPlace();
    void deletesOneAndMany();
    void readsRangesInOrder();
    void sumsMinutesPerDay();
    void refreshesImportedWorklogsInsteadOfDuplicating();
    void offersRecentIssuesAndDistinctValues();
    void storesDayMetaAndDropsEmptyOnes();

private:
    /*! An entry with every field filled in, so nothing can be silently lost. */
    static WorkEntry fullEntry();

    Database *m_database = nullptr;
};

void TestRepository::init()
{
    m_database = new Database;
    QString error;
    QVERIFY2(m_database->open(QStringLiteral(":memory:"), &error), qPrintable(error));
}

void TestRepository::cleanup()
{
    delete m_database;
    m_database = nullptr;
}

WorkEntry TestRepository::fullEntry()
{
    WorkEntry entry;
    entry.date = QDate(2026, 9, 11);
    entry.startTime = QTime(9, 15);
    entry.endTime = QTime(11, 45);
    entry.minutes = 150;
    entry.issueKey = QStringLiteral("PLAT-42");
    entry.summary = QStringLiteral("Rework the importer");
    entry.description = QStringLiteral("Split the parser out,\nadded tests.");
    entry.project = QStringLiteral("PLATFORM");
    entry.issueType = QStringLiteral("Story");
    entry.epic = QStringLiteral("Import pipeline");
    entry.sprint = QStringLiteral("Sprint 37");
    entry.component = QStringLiteral("backend");
    entry.fixVersion = QStringLiteral("2.4.0");
    entry.branch = QStringLiteral("feature/plat-42-importer");
    entry.mergeRequest = QStringLiteral("https://git.example.com/x/-/merge_requests/91");
    entry.testsheetName = QStringLiteral("PLAT-42_testsheet.xlsx");
    entry.testsheetUrl = QStringLiteral("https://jira.example.com/secure/attachment/1/ts.xlsx");
    entry.issueUrl = QStringLiteral("https://jira.example.com/browse/PLAT-42");
    entry.tags = QStringLiteral("refactor, tests");
    entry.activity = Activity::CodeReview;
    entry.status = EntryStatus::InReview;
    entry.priority = Priority::High;
    entry.location = WorkLocation::Home;
    entry.billable = false;
    entry.source = EntrySource::Jira;
    entry.externalId = QStringLiteral("jira:PLAT-42:1001");
    return entry;
}

void TestRepository::opensAndCreatesSchema()
{
    QVERIFY(m_database->isOpen());
    QCOMPARE(m_database->schemaVersion(), Database::expectedSchemaVersion());
}

void TestRepository::opensAFileDatabaseAndKeepsIt()
{
    // A file database goes through a different path than an in-memory one - it
    // creates the folder and switches the journal to WAL - so it gets its own
    // test rather than being assumed to behave the same.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("nested/folder/worklog.sqlite"));

    WorkEntry entry = fullEntry();
    {
        Database database;
        QString error;
        QVERIFY2(database.open(path, &error), qPrintable(error));
        QCOMPARE(database.schemaVersion(), Database::expectedSchemaVersion());

        WorkEntryRepository repository(database);
        QVERIFY2(repository.add(entry, &error), qPrintable(error));
    }

    QVERIFY(QFile::exists(path));

    // Reopening must find the schema already there and the entry still in it.
    Database reopened;
    QString error;
    QVERIFY2(reopened.open(path, &error), qPrintable(error));
    QCOMPARE(reopened.schemaVersion(), Database::expectedSchemaVersion());

    WorkEntryRepository repository(reopened);
    QCOMPARE(repository.count(), 1);
    QCOMPARE(repository.byId(entry.id).summary, entry.summary);
}

void TestRepository::addsAndReadsBackEveryField()
{
    WorkEntryRepository repository(*m_database);

    WorkEntry entry = fullEntry();
    QString error;
    QVERIFY2(repository.add(entry, &error), qPrintable(error));
    QVERIFY(entry.id >= 0);
    QVERIFY(entry.createdAt.isValid());

    bool found = false;
    const WorkEntry stored = repository.byId(entry.id, &found);
    QVERIFY(found);

    QCOMPARE(stored.date, entry.date);
    QCOMPARE(stored.startTime, entry.startTime);
    QCOMPARE(stored.endTime, entry.endTime);
    QCOMPARE(stored.minutes, entry.minutes);
    QCOMPARE(stored.issueKey, entry.issueKey);
    QCOMPARE(stored.summary, entry.summary);
    QCOMPARE(stored.description, entry.description);
    QCOMPARE(stored.project, entry.project);
    QCOMPARE(stored.issueType, entry.issueType);
    QCOMPARE(stored.epic, entry.epic);
    QCOMPARE(stored.sprint, entry.sprint);
    QCOMPARE(stored.component, entry.component);
    QCOMPARE(stored.fixVersion, entry.fixVersion);
    QCOMPARE(stored.branch, entry.branch);
    QCOMPARE(stored.mergeRequest, entry.mergeRequest);
    QCOMPARE(stored.testsheetName, entry.testsheetName);
    QCOMPARE(stored.testsheetUrl, entry.testsheetUrl);
    QCOMPARE(stored.issueUrl, entry.issueUrl);
    QCOMPARE(stored.tags, entry.tags);
    QCOMPARE(stored.activity, entry.activity);
    QCOMPARE(stored.status, entry.status);
    QCOMPARE(stored.priority, entry.priority);
    QCOMPARE(stored.location, entry.location);
    QCOMPARE(stored.billable, entry.billable);
    QCOMPARE(stored.source, entry.source);
    QCOMPARE(stored.externalId, entry.externalId);
}

void TestRepository::rejectsInvalidEntries()
{
    WorkEntryRepository repository(*m_database);

    WorkEntry noDuration = fullEntry();
    noDuration.minutes = 0;
    QString error;
    QVERIFY(!repository.add(noDuration, &error));
    QVERIFY(!error.isEmpty());

    WorkEntry nameless;
    nameless.date = QDate(2026, 9, 11);
    nameless.minutes = 60;
    QVERIFY(!repository.add(nameless, &error));

    WorkEntry backwards = fullEntry();
    backwards.startTime = QTime(17, 0);
    backwards.endTime = QTime(9, 0);
    QVERIFY(!repository.add(backwards, &error));

    QCOMPARE(repository.count(), 0);
}

void TestRepository::updatesInPlace()
{
    WorkEntryRepository repository(*m_database);

    WorkEntry entry = fullEntry();
    QVERIFY(repository.add(entry));

    entry.minutes = 240;
    entry.summary = QStringLiteral("Rework the importer, again");
    QString error;
    QVERIFY2(repository.update(entry, &error), qPrintable(error));

    QCOMPARE(repository.count(), 1);
    const WorkEntry stored = repository.byId(entry.id);
    QCOMPARE(stored.minutes, 240);
    QCOMPARE(stored.summary, entry.summary);

    // An entry that was never saved cannot be updated.
    WorkEntry unsaved = fullEntry();
    QVERIFY(!repository.update(unsaved, &error));
}

void TestRepository::deletesOneAndMany()
{
    WorkEntryRepository repository(*m_database);

    QVector<int> ids;
    for (int day = 1; day <= 5; ++day) {
        WorkEntry entry = fullEntry();
        entry.externalId.clear();
        entry.date = QDate(2026, 9, day);
        QVERIFY(repository.add(entry));
        ids.append(entry.id);
    }
    QCOMPARE(repository.count(), 5);

    QVERIFY(repository.remove(ids.takeFirst()));
    QCOMPARE(repository.count(), 4);

    QVERIFY(repository.removeMany(ids));
    QCOMPARE(repository.count(), 0);
}

void TestRepository::readsRangesInOrder()
{
    WorkEntryRepository repository(*m_database);

    WorkEntry afternoon = fullEntry();
    afternoon.externalId.clear();
    afternoon.date = QDate(2026, 9, 10);
    afternoon.startTime = QTime(14, 0);
    afternoon.endTime = QTime(15, 0);
    afternoon.minutes = 60;
    QVERIFY(repository.add(afternoon));

    WorkEntry morning = fullEntry();
    morning.externalId.clear();
    morning.date = QDate(2026, 9, 10);
    morning.startTime = QTime(8, 0);
    morning.endTime = QTime(9, 0);
    morning.minutes = 60;
    QVERIFY(repository.add(morning));

    WorkEntry outside = fullEntry();
    outside.externalId.clear();
    outside.date = QDate(2026, 10, 2);
    QVERIFY(repository.add(outside));

    const QVector<WorkEntry> september =
        repository.inRange(QDate(2026, 9, 1), QDate(2026, 9, 30));
    QCOMPARE(september.size(), 2);
    QCOMPARE(september.first().startTime, QTime(8, 0)); // ordered by start time

    QCOMPARE(repository.forDate(QDate(2026, 10, 2)).size(), 1);
    QCOMPARE(repository.earliestDate(), QDate(2026, 9, 10));
    QCOMPARE(repository.latestDate(), QDate(2026, 10, 2));
}

void TestRepository::sumsMinutesPerDay()
{
    WorkEntryRepository repository(*m_database);

    for (int index = 0; index < 3; ++index) {
        WorkEntry entry = fullEntry();
        entry.externalId.clear();
        entry.date = QDate(2026, 9, 10);
        entry.minutes = 100 + index;   // 100 + 101 + 102 = 303
        entry.startTime = QTime();
        entry.endTime = QTime();
        QVERIFY(repository.add(entry));
    }

    const QMap<QDate, int> totals =
        repository.minutesByDate(QDate(2026, 9, 1), QDate(2026, 9, 30));
    QCOMPARE(totals.value(QDate(2026, 9, 10)), 303);
    QCOMPARE(totals.size(), 1);
}

void TestRepository::refreshesImportedWorklogsInsteadOfDuplicating()
{
    WorkEntryRepository repository(*m_database);

    WorkEntry imported = fullEntry();
    QVERIFY(repository.addOrUpdateByExternalId(imported));
    QCOMPARE(repository.count(), 1);
    const QDateTime firstCreatedAt = repository.byId(imported.id).createdAt;

    // The same worklog, edited in Jira and imported again.
    WorkEntry again = fullEntry();
    again.minutes = 300;
    again.summary = QStringLiteral("Rework the importer (updated)");
    QVERIFY(repository.addOrUpdateByExternalId(again));

    QCOMPARE(repository.count(), 1);
    const WorkEntry stored = repository.byId(again.id);
    QCOMPARE(stored.minutes, 300);
    QCOMPARE(stored.summary, again.summary);
    QCOMPARE(stored.createdAt, firstCreatedAt); // the original creation is kept

    // A hand-made entry has no external id and must never be folded into it.
    WorkEntry manual = fullEntry();
    manual.externalId.clear();
    QVERIFY(repository.addOrUpdateByExternalId(manual));
    QCOMPARE(repository.count(), 2);

    WorkEntry anotherManual = fullEntry();
    anotherManual.externalId.clear();
    QVERIFY(repository.addOrUpdateByExternalId(anotherManual));
    QCOMPARE(repository.count(), 3);
}

void TestRepository::offersRecentIssuesAndDistinctValues()
{
    WorkEntryRepository repository(*m_database);

    WorkEntry older = fullEntry();
    older.externalId.clear();
    older.date = QDate(2026, 9, 1);
    older.issueKey = QStringLiteral("PLAT-1");
    older.sprint = QStringLiteral("Sprint 36");
    QVERIFY(repository.add(older));

    WorkEntry newer = fullEntry();
    newer.externalId.clear();
    newer.date = QDate(2026, 9, 20);
    newer.issueKey = QStringLiteral("PLAT-2");
    newer.sprint = QStringLiteral("Sprint 37");
    QVERIFY(repository.add(newer));

    WorkEntry duplicateIssue = fullEntry();
    duplicateIssue.externalId.clear();
    duplicateIssue.date = QDate(2026, 9, 21);
    duplicateIssue.issueKey = QStringLiteral("PLAT-2");
    QVERIFY(repository.add(duplicateIssue));

    const QVector<WorkEntry> recent = repository.recentIssues(10);
    QCOMPARE(recent.size(), 2);                       // one row per issue key
    QCOMPARE(recent.first().issueKey, QStringLiteral("PLAT-2")); // newest first

    const QStringList sprints = repository.distinctValues(WorkEntry::FieldSprint);
    QCOMPARE(sprints.size(), 2);
    QCOMPARE(sprints.first(), QStringLiteral("Sprint 37")); // most recently used

    // Fields that are not worth filtering on return nothing at all.
    QVERIFY(repository.distinctValues(WorkEntry::FieldDescription).isEmpty());
}

void TestRepository::storesDayMetaAndDropsEmptyOnes()
{
    DayMetaRepository repository(*m_database);
    const QDate date(2026, 12, 25);

    DayMeta holiday;
    holiday.date = date;
    holiday.type = DayType::Holiday;
    holiday.note = QStringLiteral("Christmas");
    QString error;
    QVERIFY2(repository.save(holiday, &error), qPrintable(error));

    const DayMeta stored = repository.forDate(date);
    QCOMPARE(stored.type, DayType::Holiday);
    QCOMPARE(stored.note, QStringLiteral("Christmas"));
    QCOMPARE(stored.targetMinutesOverride, -1);

    DayMeta halfDay;
    halfDay.date = QDate(2026, 12, 24);
    halfDay.type = DayType::HalfDay;
    halfDay.targetMinutesOverride = 240;
    QVERIFY(repository.save(halfDay));

    const QHash<QDate, DayMeta> december =
        repository.inRange(QDate(2026, 12, 1), QDate(2026, 12, 31));
    QCOMPARE(december.size(), 2);
    QCOMPARE(december.value(QDate(2026, 12, 24)).targetMinutesOverride, 240);
    QCOMPARE(repository.absenceDayCount(QDate(2026, 12, 1), QDate(2026, 12, 31)), 1);

    // Saving a record with nothing special in it removes the row instead.
    DayMeta reset;
    reset.date = date;
    QVERIFY(reset.isEmpty());
    QVERIFY(repository.save(reset));
    QCOMPARE(repository.inRange(QDate(2026, 12, 1), QDate(2026, 12, 31)).size(), 1);

    // A day with no record at all still answers with sensible defaults.
    const DayMeta unknown = repository.forDate(QDate(2026, 7, 4));
    QCOMPARE(unknown.type, DayType::Workday);
    QCOMPARE(unknown.date, QDate(2026, 7, 4));
}

QTEST_GUILESS_MAIN(TestRepository)
#include "tst_repository.moc"
