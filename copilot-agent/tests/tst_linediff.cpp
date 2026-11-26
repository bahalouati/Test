#include "linediff.h"

#include <QtTest>

class TestLineDiff : public QObject
{
    Q_OBJECT
private slots:
    void newFile()
    {
        const QString d = LineDiff::unified(QString(), "a\nb\n", "x.txt");
        QVERIFY(d.startsWith("--- /dev/null\n+++ b/x.txt\n"));
        QVERIFY(d.contains("@@ -0,0 +1,2 @@\n+a\n+b\n"));
        QCOMPARE(LineDiff::stats(d).added, 2);
        QCOMPARE(LineDiff::stats(d).removed, 0);
    }

    void singleChangeWithContext()
    {
        QStringList before, after;
        for (int i = 1; i <= 20; ++i)
            before << QString::number(i);
        after = before;
        after[9] = "ten";
        const QString d = LineDiff::unified(before.join('\n'), after.join('\n'), "n");
        QVERIFY2(d.contains("@@ -7,7 +7,7 @@\n 7\n 8\n 9\n-10\n+ten\n 11\n 12\n 13\n"), qPrintable(d));
        QCOMPARE(LineDiff::stats(d).added, 1);
        QCOMPARE(LineDiff::stats(d).removed, 1);
    }

    void separateHunks()
    {
        QStringList before;
        for (int i = 1; i <= 40; ++i)
            before << QString::number(i);
        QStringList after = before;
        after[2] = "x";
        after[35] = "y";
        const QString d = LineDiff::unified(before.join('\n'), after.join('\n'), "n");
        QCOMPARE(d.count("@@ -"), 2);
    }

    void identical()
    {
        QVERIFY(LineDiff::unified("a\n", "a\n", "f").contains("(no changes)"));
    }

    void hugeInputStaysFast()
    {
        QStringList a, b;
        for (int i = 0; i < 5000; ++i) {
            a << QString("line %1").arg(i);
            b << QString("LINE %1").arg(i);
        }
        QElapsedTimer t;
        t.start();
        const QString d = LineDiff::unified(a.join('\n'), b.join('\n'), "big");
        QVERIFY(t.elapsed() < 3000);
        QCOMPARE(LineDiff::stats(d).added, 5000);
    }
};

QTEST_GUILESS_MAIN(TestLineDiff)
#include "tst_linediff.moc"
