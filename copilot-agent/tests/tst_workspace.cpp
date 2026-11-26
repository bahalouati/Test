#include "workspace.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

static void put(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

static AgentAction act(AgentAction::Type t, const QString &path)
{
    AgentAction a;
    a.type = t;
    a.path = path;
    return a;
}

class TestWorkspace : public QObject
{
    Q_OBJECT
private slots:
    void refusesEscapes()
    {
        QTemporaryDir outer;
        const QString root = outer.path() + "/proj";
        QDir().mkpath(root);
        Workspace ws(root);
        QString err;
        QVERIFY(ws.resolve("../x", &err).isEmpty());
        QVERIFY(ws.resolve("a/../../x", &err).isEmpty());
        QVERIFY(ws.resolve("/etc/passwd", &err).isEmpty());
        QVERIFY(ws.resolve("~/x", &err).isEmpty());
        QVERIFY(!ws.resolve("a/../b.txt", &err).isEmpty());
        QVERIFY(!ws.resolve("new/dir/file.txt", &err).isEmpty());
        // A sibling whose name starts with the root's name is still outside.
        QDir().mkpath(outer.path() + "/proj2");
        QVERIFY(ws.resolve("../proj2/x", &err).isEmpty());
    }

    void refusesSymlinkOut()
    {
#ifndef Q_OS_WIN
        QTemporaryDir outer;
        const QString root = outer.path() + "/proj";
        QDir().mkpath(root);
        QDir().mkpath(outer.path() + "/secret");
        QVERIFY(QFile::link(outer.path() + "/secret", root + "/link"));
        Workspace ws(root);
        QString err;
        QVERIFY(ws.resolve("link/file", &err).isEmpty());
        QVERIFY(err.contains("outside"));
        AgentAction w = act(AgentAction::WriteFile, "link/pwn.txt");
        w.content = "x";
        QVERIFY(!ws.writeFile(w).ok);
        QVERIFY(!QFile::exists(outer.path() + "/secret/pwn.txt"));
#endif
    }

    void readsAndRanges()
    {
        QTemporaryDir d;
        put(d.path() + "/a.txt", "one\ntwo\nthree\nfour");
        Workspace ws(d.path());
        ActionResult r = ws.readFile(act(AgentAction::ReadFile, "a.txt"));
        QVERIFY(r.ok);
        QCOMPARE(r.output, QStringLiteral("one\ntwo\nthree\nfour"));
        AgentAction ranged = act(AgentAction::ReadFile, "a.txt");
        ranged.startLine = 2;
        ranged.endLine = 3;
        r = ws.readFile(ranged);
        QCOMPARE(r.output, QStringLiteral("two\nthree"));
        QVERIFY(r.status.contains("2-3 of 4"));
        QVERIFY(!ws.readFile(act(AgentAction::ReadFile, "missing")).ok);

        put(d.path() + "/bin.dat", QByteArray("ab\0cd", 5));
        QVERIFY(!ws.readFile(act(AgentAction::ReadFile, "bin.dat")).ok);
    }

    void listsAndSkipsNoise()
    {
        QTemporaryDir d;
        put(d.path() + "/src/main.cpp", "x");
        put(d.path() + "/.git/HEAD", "ref");
        put(d.path() + "/build/out.o", "o");
        Workspace ws(d.path());
        AgentAction a = act(AgentAction::ListDir, ".");
        a.depth = 3;
        const ActionResult r = ws.listDir(a);
        QVERIFY(r.ok);
        QVERIFY(r.output.contains("src/main.cpp"));
        QVERIFY(r.output.contains(".git/  (contents not listed)"));
        QVERIFY(!r.output.contains("HEAD"));
        QVERIFY(!r.output.contains("out.o"));
    }

    void writesAndReplaces()
    {
        QTemporaryDir d;
        Workspace ws(d.path());
        AgentAction w = act(AgentAction::WriteFile, "deep/new.txt");
        w.content = "alpha\nbeta\nalpha\n";
        QVERIFY(ws.writeFile(w).ok);

        AgentAction rep = act(AgentAction::ReplaceInFile, "deep/new.txt");
        rep.oldText = "alpha";
        rep.newText = "x";
        ActionResult r = ws.replaceInFile(rep);
        QVERIFY(!r.ok);
        QVERIFY(r.status.contains("2 times"));

        rep.oldText = "beta\nalpha";
        rep.newText = "gamma";
        QVERIFY(ws.replaceInFile(rep).ok);
        QCOMPARE(ws.readFile(act(AgentAction::ReadFile, "deep/new.txt")).output, QStringLiteral("alpha\ngamma\n"));

        rep.oldText = "nope";
        QVERIFY(ws.replaceInFile(rep).status.contains("not found"));
    }

    void replaceKeepsCrlf()
    {
        QTemporaryDir d;
        put(d.path() + "/w.txt", "a\r\nb\r\nc\r\n");
        Workspace ws(d.path());
        AgentAction rep = act(AgentAction::ReplaceInFile, "w.txt");
        rep.oldText = "a\nb";
        rep.newText = "a\nB";
        QVERIFY(ws.replaceInFile(rep).ok);
        QFile f(d.path() + "/w.txt");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("a\r\nB\r\nc\r\n"));
    }
};

QTEST_GUILESS_MAIN(TestWorkspace)
#include "tst_workspace.moc"
