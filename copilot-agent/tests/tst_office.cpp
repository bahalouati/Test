// Word and Excel support, against realistic files in tests/fixtures
// (made with python-docx / openpyxl, then given Excel's shared strings and
// calculation chain).
#include "officefile.h"
#include "workspace.h"

#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

static QByteArray fixture(const char *name)
{
    QFile f(QStringLiteral(FIXTURES_DIR "/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return f.readAll();
}

static Office::Package package(const char *name)
{
    Office::Package p;
    QString err;
    if (!p.load(fixture(name), &err))
        qFatal("cannot load %s: %s", name, qPrintable(err));
    return p;
}

class TestOffice : public QObject
{
    Q_OBJECT
private slots:
    void kinds()
    {
        QCOMPARE(Office::kindOf("a/Spec.DOCX"), Office::Word);
        QCOMPARE(Office::kindOf("b.xlsm"), Office::Excel);
        QCOMPARE(Office::kindOf("c.doc"), Office::None);
        QCOMPARE(Office::kindOf("main.cpp"), Office::None);
    }

    void wordText()
    {
        QString err;
        const QString t = Office::wordText(package("spec.docx"), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY2(t.contains("# Timer requirements"), qPrintable(t));
        QVERIFY(t.contains("The timer shows the current time every second & uses <QTimer>."));
        QVERIFY(t.contains("| Field | Value |"));
        QVERIFY(t.contains("| Interval | 1000 ms |"));
        QVERIFY(t.contains("  Leading spaces matter."));
    }

    void wordReplaceWithinOneRun()
    {
        Office::Package p = package("spec.docx");
        const Office::Package original = p;
        QString err, note;
        QVERIFY(Office::wordReplace(p, "resets the count", "restarts at 0 & <keeps> going", &err, &note));
        QVERIFY(note.isEmpty());
        const QString t = Office::wordText(p, &err);
        QVERIFY2(t.contains("Start button restarts at 0 & <keeps> going."), qPrintable(t));
        // Only the document text part changed; everything else is identical.
        for (const QString &name : original.order)
            if (name != QLatin1String("word/document.xml"))
                QCOMPARE(p.files.value(name), original.files.value(name));
        // And it survives a save/load round trip.
        Office::Package reloaded;
        QVERIFY(reloaded.load(p.save(), &err));
        QVERIFY(Office::wordText(reloaded, &err).contains("restarts at 0 & <keeps> going"));
        QCOMPARE(reloaded.order, original.order);
    }

    void wordReplaceAcrossFormatting()
    {
        Office::Package p = package("spec.docx");
        QString err, note;
        // "current time" is bold; the old text spans plain + bold + plain.
        QVERIFY(Office::wordReplace(p, "shows the current time every", "displays the local time each", &err, &note));
        QVERIFY2(note.contains("3 differently formatted parts"), qPrintable(note));
        QVERIFY(Office::wordText(p, &err).contains("The timer displays the local time each second & uses <QTimer>."));
    }

    void wordEdgeSpacesArePreserved()
    {
        Office::Package p = package("spec.docx");
        QString err, note;
        QVERIFY(Office::wordReplace(p, "Start button", "  Start button", &err, &note));
        QVERIFY(Office::wordText(p, &err).contains("\n  Start button resets the count."));
        QVERIFY(p.text("word/document.xml").contains("xml:space=\"preserve\">  Start button"));
    }

    void wordReplaceErrors()
    {
        Office::Package p = package("spec.docx");
        QString err, note;
        QVERIFY(!Office::wordReplace(p, "not in the document", "x", &err, &note));
        QVERIFY(err.contains("not found"));
        QVERIFY(!Office::wordReplace(p, "the", "x", &err, &note));
        QVERIFY(err.contains("occurs"));
        QVERIFY(!Office::wordReplace(p, "resets", "a\nb", &err, &note));
        QVERIFY(err.contains("line breaks"));
        QVERIFY(!Office::wordReplace(p, "count.Field", "x", &err, &note));   // across paragraphs
    }

    void excelText()
    {
        QString err;
        const QString t = Office::excelText(package("budget.xlsx"), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY2(t.contains("# Sheet \"Budget\"\nA1 = Item\nB1 = Cost\n"), qPrintable(t));
        QVERIFY(t.contains("A2 = Qt license\nB2 = 120\n"));
        QVERIFY(t.contains("B3 = 900.5"));
        QVERIFY(t.contains("B4 = =SUM(B2:B3)   [value: 1020.5]"));
        QVERIFY(t.contains("# Sheet \"Notes\"\nA1 = Reviewed\nB1 = TRUE\nA2 = Due\nB2 = 2026-12-01"));
    }

    void excelSetCells()
    {
        Office::Package p = package("budget.xlsx");
        QString err, used;
        const QJsonObject cells{
            {"B3", 950},                  // existing, yellow fill style kept
            {"C2", "per year"},           // new cell in an existing row, after B2
            {"A6", "Extra"},              // new row after the last
            {"B5", "=B4*2"},              // new row between existing ones
            {"a2", "Qt licence"},         // lower-case reference
            {"B2", QJsonValue()},         // emptied
        };
        QVERIFY2(Office::excelSetCells(p, "budget", cells, &err, &used), qPrintable(err));
        QCOMPARE(used, QStringLiteral("Budget"));
        const QString t = Office::excelText(p, &err);
        QVERIFY2(t.contains("A2 = Qt licence\nC2 = per year\nA3 = Laptop\nB3 = 950\n"), qPrintable(t));
        QVERIFY(t.contains("B5 = =B4*2\nA6 = Extra"));
        QVERIFY(!t.contains("B2 = 120"));
        const QString sheet = p.text("xl/worksheets/sheet1.xml");
        QVERIFY2(sheet.contains("<c r=\"B3\" s=\"2\"><v>950</v></c>"), qPrintable(sheet));
        QVERIFY(sheet.contains("<row r=\"4\">") && sheet.indexOf("<row r=\"5\">") > sheet.indexOf("<row r=\"4\">"));
        // Formulas are recalculated by Excel on open; the stale chain is gone.
        QVERIFY(!p.has("xl/calcChain.xml"));
        QVERIFY(!p.text("[Content_Types].xml").contains("calcChain"));
        QVERIFY(!p.text("xl/_rels/workbook.xml.rels").contains("calcChain"));
        QCOMPARE(p.text("xl/workbook.xml").count("fullCalcOnLoad=\"1\""), 1);
        // Stale cached results are gone, so nothing shows the old total.
        QVERIFY2(t.contains("B4 = =SUM(B2:B3)\n"), qPrintable(t));
        QVERIFY(!t.contains("1020.5"));
        // The other sheet is untouched.
        QCOMPARE(p.files.value("xl/worksheets/sheet2.xml"), package("budget.xlsx").files.value("xl/worksheets/sheet2.xml"));

        Office::Package q = package("budget.xlsx");
        QVERIFY(Office::excelSetCells(q, "Notes", QJsonObject{{"B1", false}}, &err, &used));
        QVERIFY(Office::excelText(q, &err).contains("B1 = FALSE"));
    }

    void excelErrors()
    {
        Office::Package p = package("budget.xlsx");
        QString err, used;
        QVERIFY(!Office::excelSetCells(p, "Nope", QJsonObject{{"A1", 1}}, &err, &used));
        QVERIFY(err.contains("\"Budget\", \"Notes\""));
        QVERIFY(!Office::excelSetCells(p, QString(), QJsonObject{{"1A", 1}}, &err, &used));
        QVERIFY(err.contains("not a cell reference"));
        QVERIFY(!Office::excelSetCells(p, QString(), QJsonObject{{"A1", QJsonObject()}}, &err, &used));
    }

    void workspaceIntegration()
    {
        QTemporaryDir d;
        QDir().mkpath(d.path() + "/docs");
        for (const char *name : {"spec.docx", "budget.xlsx"}) {
            QFile f(d.path() + "/docs/" + QLatin1String(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(fixture(name));
        }
        Workspace ws(d.path());

        AgentAction read;
        read.type = AgentAction::ReadFile;
        read.path = "docs/budget.xlsx";
        ActionResult r = ws.readFile(read);
        QVERIFY2(r.ok && r.output.contains("B4 = =SUM(B2:B3)"), qPrintable(r.status));

        AgentAction search;
        search.type = AgentAction::Search;
        search.query = "1000 ms";
        r = ws.search(search);
        QVERIFY2(r.output.contains("docs/spec.docx:"), qPrintable(r.output));

        AgentAction write;
        write.type = AgentAction::WriteFile;
        write.path = "docs/spec.docx";
        write.content = "plain text";
        r = ws.writeFile(write);
        QVERIFY(!r.ok && r.status.contains("would destroy it"));
        write.path = "docs/new.xlsx";
        QVERIFY(!ws.writeFile(write).ok);

        AgentAction rep;
        rep.type = AgentAction::ReplaceInFile;
        rep.path = "docs/spec.docx";
        rep.oldText = "1000 ms";
        rep.newText = "500 ms";
        r = ws.replaceInFile(rep);
        QVERIFY2(r.ok && r.status.contains("formatting kept"), qPrintable(r.status));
        read.path = "docs/spec.docx";
        QVERIFY(ws.readFile(read).output.contains("| Interval | 500 ms |"));

        rep.path = "docs/budget.xlsx";
        QVERIFY(ws.replaceInFile(rep).status.contains("use set_cells"));

        AgentAction cells;
        cells.type = AgentAction::SetCells;
        cells.path = "docs/budget.xlsx";
        cells.cells = QJsonObject{{"B2", 99}};
        r = ws.setCells(cells);
        QVERIFY2(r.ok && r.status.contains("changed sheet \"Budget\""), qPrintable(r.status));
        cells.path = "docs/spec.docx";
        QVERIFY(ws.setCells(cells).status.contains("works on Excel"));
    }
};

QTEST_GUILESS_MAIN(TestOffice)
#include "tst_office.moc"
