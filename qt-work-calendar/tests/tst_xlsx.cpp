#include "data/Statistics.h"
#include "export/ExcelReport.h"
#include "export/XlsxWriter.h"

#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

/*!
 * \brief Checks the workbook writer produces a well-formed package.
 *
 * There is no spreadsheet library to check against, so the tests verify the
 * two things that actually break: the zip container and the cell references.
 * A companion script in the repository opens a produced file with a real
 * spreadsheet library; these tests keep the build honest without one.
 */
class TestXlsx : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void namesColumnsLikeExcel();
    void producesAValidZipContainer();
    void reusesIdenticalFormats();
    void escapesTextThatWouldBreakTheXml();
    void writesTheFullReport();

private:
    /*! The offset of the end-of-central-directory record, or -1. */
    static int endOfCentralDirectory(const QByteArray &archive);
};

void TestXlsx::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("WorkCalendarTests"));
    QCoreApplication::setApplicationName(QStringLiteral("XlsxTest"));
}

int TestXlsx::endOfCentralDirectory(const QByteArray &archive)
{
    const QByteArray signature("PK\x05\x06", 4);
    return static_cast<int>(archive.lastIndexOf(signature));
}

void TestXlsx::namesColumnsLikeExcel()
{
    QCOMPARE(XlsxSheet::columnName(1), QStringLiteral("A"));
    QCOMPARE(XlsxSheet::columnName(26), QStringLiteral("Z"));
    QCOMPARE(XlsxSheet::columnName(27), QStringLiteral("AA"));
    QCOMPARE(XlsxSheet::columnName(28), QStringLiteral("AB"));
    QCOMPARE(XlsxSheet::columnName(52), QStringLiteral("AZ"));
    QCOMPARE(XlsxSheet::columnName(53), QStringLiteral("BA"));
    QCOMPARE(XlsxSheet::cellReference(2, 3), QStringLiteral("C2"));
    QCOMPARE(XlsxSheet::cellReference(10, 27), QStringLiteral("AA10"));
}

void TestXlsx::producesAValidZipContainer()
{
    XlsxWriter workbook;

    XlsxFormat header;
    header.bold = true;
    header.fillColor = QStringLiteral("1F4E78");
    header.fontColor = QStringLiteral("FFFFFF");
    const int headerFormat = workbook.addFormat(header);

    XlsxSheet *sheet = workbook.addSheet(QStringLiteral("Worklogs"));
    sheet->writeString(1, 1, QStringLiteral("Date"), headerFormat);
    sheet->writeString(1, 2, QStringLiteral("Hours"), headerFormat);
    sheet->writeString(2, 1, QStringLiteral("2026-09-11"));
    sheet->writeNumber(2, 2, 7.5);
    sheet->writeHyperlink(3, 1, QStringLiteral("MR 91"),
                          QStringLiteral("https://git.example.com/x/-/merge_requests/91"));
    sheet->setColumnWidth(1, 18);
    sheet->setRowHeight(3, 40);
    sheet->freezePanes(1, 0);
    sheet->setAutoFilter(1, 1, 3, 2);
    sheet->mergeCells(4, 1, 4, 2);

    const QByteArray archive = workbook.toByteArray();

    QVERIFY(archive.startsWith(QByteArray("PK\x03\x04", 4)));

    const int eocd = endOfCentralDirectory(archive);
    QVERIFY(eocd > 0);

    // Seven parts: content types, root rels, workbook, workbook rels, styles,
    // the sheet and the sheet's hyperlink relationships.
    const quint16 entryCount = static_cast<quint8>(archive.at(eocd + 10))
                               | (static_cast<quint8>(archive.at(eocd + 11)) << 8);
    QCOMPARE(static_cast<int>(entryCount), 7);

    QVERIFY(archive.contains(QByteArray("[Content_Types].xml")));
    QVERIFY(archive.contains(QByteArray("xl/worksheets/sheet1.xml")));
    QVERIFY(archive.contains(QByteArray("xl/worksheets/_rels/sheet1.xml.rels")));
    QVERIFY(archive.contains(QByteArray("xl/styles.xml")));

    // Spot-check the sheet body: references, the number, and the structure.
    QVERIFY(archive.contains(QByteArray("<c r=\"A1\"")));
    QVERIFY(archive.contains(QByteArray("<c r=\"B2\"><v>7.500000</v></c>")));
    QVERIFY(archive.contains(QByteArray("<autoFilter ref=\"A1:B3\"/>")));
    QVERIFY(archive.contains(QByteArray("<mergeCell ref=\"A4:B4\"/>")));
    QVERIFY(archive.contains(QByteArray("state=\"frozen\"")));
    QVERIFY(archive.contains(QByteArray("TargetMode=\"External\"")));

    // Writing the same workbook twice must give exactly the same bytes.
    QCOMPARE(workbook.toByteArray(), archive);
}

void TestXlsx::reusesIdenticalFormats()
{
    XlsxWriter workbook;

    XlsxFormat bold;
    bold.bold = true;

    XlsxFormat alsoBold;
    alsoBold.bold = true;

    XlsxFormat italic;
    italic.italic = true;

    QCOMPARE(workbook.addFormat(bold), workbook.addFormat(alsoBold));
    QVERIFY(workbook.addFormat(italic) != workbook.addFormat(bold));

    // Index 0 belongs to the built-in default, so registered formats start at 1.
    QCOMPARE(workbook.addFormat(bold), 1);
}

void TestXlsx::escapesTextThatWouldBreakTheXml()
{
    XlsxWriter workbook;
    XlsxSheet *sheet = workbook.addSheet(QStringLiteral("Notes"));
    sheet->writeString(1, 1, QStringLiteral("a < b & c > d \"quoted\""));

    const QByteArray archive = workbook.toByteArray();
    QVERIFY(archive.contains(QByteArray("a &lt; b &amp; c &gt; d &quot;quoted&quot;")));
    QVERIFY(!archive.contains(QByteArray("<t xml:space=\"preserve\">a < b")));
}

void TestXlsx::writesTheFullReport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("report.xlsx"));

    Settings settings;

    WorkEntry entry;
    entry.date = QDate(2026, 9, 11);
    entry.minutes = 450;
    entry.issueKey = QStringLiteral("PLAT-42");
    entry.summary = QStringLiteral("Rework the importer");
    entry.sprint = QStringLiteral("Sprint 37");
    entry.fixVersion = QStringLiteral("2.4.0");
    entry.mergeRequest = QStringLiteral("https://git.example.com/x/-/merge_requests/91");

    QVector<WorkEntry> entries;
    entries << entry;

    const QVector<DaySummary> days = Statistics::buildDaySummaries(
        QDate(2026, 9, 1), QDate(2026, 9, 30), entries, QHash<QDate, DayMeta>(), settings);

    ExcelReport::Options options;
    QString error;
    QVERIFY2(ExcelReport::write(path, entries, days, settings, options, &error),
             qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray archive = file.readAll();
    file.close();

    QVERIFY(archive.size() > 1000);
    QVERIFY(archive.startsWith(QByteArray("PK\x03\x04", 4)));
    QVERIFY(archive.contains(QByteArray("xl/worksheets/sheet3.xml"))); // three sheets
    QVERIFY(archive.contains(QByteArray("Worklogs")));
    QVERIFY(archive.contains(QByteArray("Calendar")));
    QVERIFY(archive.contains(QByteArray("Summary")));
    QVERIFY(archive.contains(QByteArray("PLAT-42")));

    QCOMPARE(ExcelReport::suggestedFileName(QDate(2026, 9, 1), QDate(2026, 9, 30)),
             QStringLiteral("Worklog_2026_09.xlsx"));
}

QTEST_GUILESS_MAIN(TestXlsx)
#include "tst_xlsx.moc"
