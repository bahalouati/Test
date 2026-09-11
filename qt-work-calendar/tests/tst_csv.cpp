#include "export/CsvIo.h"

#include <QTemporaryDir>
#include <QtTest>

/*!
 * \brief Checks that a CSV written here reads back identically.
 *
 * The hard cases are the ones a summary or a note brings in: commas, quotes and
 * line breaks inside a field.
 */
class TestCsv : public QObject
{
    Q_OBJECT

private slots:
    void quotesOnlyWhenNecessary();
    void parsesQuotedFields();
    void roundTripsEveryField();
    void importsHandWrittenHeaders();
    void reportsUnusableRowsWithoutStopping();
    void refusesAFileWithNoRecognisableColumns();

private:
    static WorkEntry awkwardEntry();
};

WorkEntry TestCsv::awkwardEntry()
{
    WorkEntry entry;
    entry.date = QDate(2026, 9, 11);
    entry.minutes = 150;
    entry.issueKey = QStringLiteral("PLAT-42");
    entry.summary = QStringLiteral("Fix parsing of \"quoted\", comma-heavy input");
    entry.description = QStringLiteral("First line\nSecond line, with a comma");
    entry.sprint = QStringLiteral("Sprint 37");
    entry.fixVersion = QStringLiteral("2.4.0");
    entry.activity = Activity::BugFix;
    entry.status = EntryStatus::Done;
    entry.billable = false;
    return entry;
}

void TestCsv::quotesOnlyWhenNecessary()
{
    QCOMPARE(CsvIo::quote(QStringLiteral("plain")), QStringLiteral("plain"));
    QCOMPARE(CsvIo::quote(QStringLiteral("a,b")), QStringLiteral("\"a,b\""));
    QCOMPARE(CsvIo::quote(QStringLiteral("say \"hi\"")), QStringLiteral("\"say \"\"hi\"\"\""));
    QCOMPARE(CsvIo::quote(QStringLiteral("two\nlines")), QStringLiteral("\"two\nlines\""));
}

void TestCsv::parsesQuotedFields()
{
    const QStringList fields = CsvIo::parseLine(
        QStringLiteral("2026-09-11,\"a,b\",\"say \"\"hi\"\"\",plain"));

    QCOMPARE(fields.size(), 4);
    QCOMPARE(fields.at(0), QStringLiteral("2026-09-11"));
    QCOMPARE(fields.at(1), QStringLiteral("a,b"));
    QCOMPARE(fields.at(2), QStringLiteral("say \"hi\""));
    QCOMPARE(fields.at(3), QStringLiteral("plain"));
}

void TestCsv::roundTripsEveryField()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("worklog.csv"));

    QVector<WorkEntry> written;
    written << awkwardEntry();

    QString error;
    QVERIFY2(CsvIo::writeEntries(path, written, WorkEntry::allFields(), &error),
             qPrintable(error));

    QVector<WorkEntry> read;
    CsvIo::ImportResult result;
    QVERIFY2(CsvIo::readEntries(path, &read, &result, &error), qPrintable(error));

    QCOMPARE(result.imported, 1);
    QCOMPARE(result.skipped, 0);
    QCOMPARE(read.size(), 1);

    const WorkEntry &original = written.first();
    const WorkEntry &restored = read.first();
    QCOMPARE(restored.date, original.date);
    QCOMPARE(restored.minutes, original.minutes);
    QCOMPARE(restored.issueKey, original.issueKey);
    QCOMPARE(restored.summary, original.summary);
    QCOMPARE(restored.description, original.description);
    QCOMPARE(restored.sprint, original.sprint);
    QCOMPARE(restored.fixVersion, original.fixVersion);
    QCOMPARE(restored.activity, original.activity);
    QCOMPARE(restored.status, original.status);
    QCOMPARE(restored.billable, original.billable);

    // Imported rows are marked as such, never as hand-typed ones.
    QCOMPARE(restored.source, EntrySource::Csv);
}

void TestCsv::importsHandWrittenHeaders()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("hand.csv"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("Date,Issue,Summary,Duration,Activity\n"
               "2026-09-11,PLAT-7,Wrote the importer,2h30,Development\n"
               "2026-09-12,PLAT-8,Reviewed it,45m,Code review\n");
    file.close();

    QVector<WorkEntry> entries;
    CsvIo::ImportResult result;
    QString error;
    QVERIFY2(CsvIo::readEntries(path, &entries, &result, &error), qPrintable(error));

    QCOMPARE(result.imported, 2);
    QCOMPARE(entries.at(0).minutes, 150);
    QCOMPARE(entries.at(0).activity, Activity::Development);
    QCOMPARE(entries.at(1).minutes, 45);
    QCOMPARE(entries.at(1).activity, Activity::CodeReview); // matched by display name
}

void TestCsv::reportsUnusableRowsWithoutStopping()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("mixed.csv"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("Date,Issue,Summary,Duration\n"
               "2026-09-11,PLAT-7,Good row,2h\n"
               "2026-09-12,PLAT-8,No duration,\n"
               "not-a-date,PLAT-9,Bad date,1h\n"
               "2026-09-13,PLAT-10,Another good row,30m\n");
    file.close();

    QVector<WorkEntry> entries;
    CsvIo::ImportResult result;
    QVERIFY(CsvIo::readEntries(path, &entries, &result, nullptr));

    QCOMPARE(result.rowsRead, 4);
    QCOMPARE(result.imported, 2);
    QCOMPARE(result.skipped, 2);
    QCOMPARE(result.problems.size(), 2);
    QVERIFY(result.problems.first().startsWith(QStringLiteral("Row 3")));
}

void TestCsv::refusesAFileWithNoRecognisableColumns()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("wrong.csv"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("alpha,beta,gamma\n1,2,3\n");
    file.close();

    QVector<WorkEntry> entries;
    CsvIo::ImportResult result;
    QString error;
    QVERIFY(!CsvIo::readEntries(path, &entries, &result, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(entries.isEmpty());
}

QTEST_GUILESS_MAIN(TestCsv)
#include "tst_csv.moc"
