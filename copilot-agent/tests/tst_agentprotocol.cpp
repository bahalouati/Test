#include "agentprotocol.h"

#include <QtTest>

using namespace AgentProtocol;

class TestAgentProtocol : public QObject
{
    Q_OBJECT
private slots:
    void parsesEachAction()
    {
        const QList<AgentAction> a = parseBlocks({
            "{\"action\":\"read_file\",\"path\":\"./src/main.cpp\",\"start_line\":3,\"end_line\":9}",
            "{\"action\":\"list_dir\",\"path\":\"src\",\"depth\":2}",
            "{\"action\":\"write_file\",\"path\":\"a.txt\",\"content\":\"x\\ny\\n\"}",
            "{\"action\":\"replace_in_file\",\"path\":\"a.txt\",\"old\":\"x\",\"new\":\"\"}",
            "{\"action\":\"run_command\",\"command\":\"make test\",\"cwd\":\"build\",\"timeout\":\"60\"}",
            "{\"action\":\"done\",\"summary\":\"all good\"}",
        });
        QCOMPARE(a.size(), 6);
        QCOMPARE(a[0].type, AgentAction::ReadFile);
        QCOMPARE(a[0].path, QStringLiteral("src/main.cpp"));
        QCOMPARE(a[0].startLine, 3);
        QCOMPARE(a[0].endLine, 9);
        QCOMPARE(a[1].type, AgentAction::ListDir);
        QCOMPARE(a[1].depth, 2);
        QCOMPARE(a[2].type, AgentAction::WriteFile);
        QCOMPARE(a[2].content, QStringLiteral("x\ny\n"));
        QCOMPARE(a[3].type, AgentAction::ReplaceInFile);
        QCOMPARE(a[3].newText, QString());
        QCOMPARE(a[4].type, AgentAction::RunCommand);
        QCOMPARE(a[4].timeoutSec, 60);
        QCOMPARE(a[4].cwd, QStringLiteral("build"));
        QCOMPARE(a[5].type, AgentAction::Done);
        QVERIFY(a[2].needsApproval() && a[3].needsApproval() && a[4].needsApproval());
        QVERIFY(!a[0].needsApproval() && !a[1].needsApproval());
    }

    void toleratesLeakedLabelAndArrays()
    {
        const QList<AgentAction> a = parseBlocks({
            "agent\n{\"action\":\"list_dir\"}",
            "[{\"action\":\"read_file\",\"path\":\"a\"},{\"action\":\"read_file\",\"path\":\"b\"}]",
            "{\"actions\":[{\"action\":\"done\"}]}",
        });
        QCOMPARE(a.size(), 4);
        QCOMPARE(a[0].path, QStringLiteral("."));
        QCOMPARE(a[2].path, QStringLiteral("b"));
        QCOMPARE(a[3].type, AgentAction::Done);
    }

    void ignoresOrdinaryCode()
    {
        const QList<AgentAction> a = parseBlocks({
            "int main() { return 0; }",
            "{\"name\":\"package\",\"version\":\"1.0\"}",
            "",
        });
        QVERIFY(a.isEmpty());
    }

    void reportsBrokenActions()
    {
        const QList<AgentAction> a = parseBlocks({
            "{\"action\":\"write_file\",\"path\":\"a\",\"content\":\"unterminated}",
            "{\"action\":\"write_file\",\"path\":\"a\"}",
            "{\"action\":\"format_disk\"}",
            "{\"action\":\"replace_in_file\",\"path\":\"a\",\"old\":\"\",\"new\":\"x\"}",
        });
        QCOMPARE(a.size(), 4);
        for (const AgentAction &x : a)
            QCOMPARE(x.type, AgentAction::Invalid);
        QVERIFY(a[0].error.contains("invalid JSON"));
        QVERIFY(a[1].error.contains("content"));
        QVERIFY(a[2].error.contains("format_disk"));
    }

    void extractsRawFences()
    {
        const QString md = "Sure.\n```agent\n{\"action\":\"list_dir\"}\n```\ntext\n```python\nprint(1)\n```\n"
                           "````json\n{\"action\":\"done\",\"summary\":\"has ``` inside\"}\n````\n";
        const QStringList b = extractFencedBlocks(md);
        QCOMPARE(b.size(), 3);              // the python block too...
        QCOMPARE(parseBlocks(b).size(), 2); // ...which the parser drops
    }

    void pastedReplies()
    {
        // Copied with the copy button: markdown with fences.
        const QString md = "Let me look.\n\n```agent\n{\"action\":\"list_dir\"}\n```\n";
        QCOMPARE(parseBlocks(blocksFromReplyText(md)).size(), 1);

        // Selected by hand: the fences are gone, the labels and braces stay.
        const QString plain = "Let me look.\nagent\n{\"action\":\"read_file\",\"path\":\"a{b}.txt\"}\n"
                              "agent\n{\"action\":\"write_file\",\"path\":\"x\",\"content\":\"}\\\" {\"}\n"
                              "Then I will check.";
        const QList<AgentAction> a = parseBlocks(blocksFromReplyText(plain));
        QCOMPARE(a.size(), 2);
        QCOMPARE(a[0].path, QStringLiteral("a{b}.txt"));
        QCOMPARE(a[1].content, QStringLiteral("}\" {"));

        QVERIFY(parseBlocks(blocksFromReplyText("Which database should I use?")).isEmpty());
    }

    void fenceOutgrowsBackticks()
    {
        QCOMPARE(fence("a"), QStringLiteral("```\na\n```"));
        QVERIFY(fence("x ```` y").startsWith("`````\n"));
    }

    void truncatesTheMiddle()
    {
        const QString big = QString(500, 'a') + QString(500, 'b');
        const QString t = truncateMiddle(big, 100);
        QVERIFY(t.startsWith(QString(40, 'a')));
        QVERIFY(t.endsWith(QString(60, 'b')));
        QVERIFY(t.contains("900 characters omitted"));
        QCOMPARE(truncateMiddle("short", 100), QStringLiteral("short"));
    }

    void resultsStayWithinBudget()
    {
        QList<ActionResult> rs;
        for (int i = 0; i < 3; ++i) {
            ActionResult r;
            r.action.type = AgentAction::ReadFile;
            r.action.path = QString("f%1").arg(i);
            r.ok = true;
            r.status = "ok";
            r.output = QString(20000, QChar('a' + i));
            rs << r;
        }
        const QString msg = formatResults(rs, 4, 12000);
        QVERIFY2(msg.size() < 13000, qPrintable(QString::number(msg.size())));
        QVERIFY(msg.contains("step 4"));
        QVERIFY(msg.contains("read_file f2 -> ok"));
        QVERIFY(msg.contains("done action"));
    }

    void promptHasNoFencedExamples()
    {
        // Our own message must never contain a parseable block.
        const QString p = taskPrompt("do it", "proj", "Linux", "/bin/bash");
        QVERIFY(extractFencedBlocks(p).isEmpty());
        QVERIFY(!p.contains("```"));
        QVERIFY(p.contains("do it"));
        QVERIFY(p.contains("/bin/bash"));
    }
};

QTEST_GUILESS_MAIN(TestAgentProtocol)
#include "tst_agentprotocol.moc"
