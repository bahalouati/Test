#include "ignorerules.h"
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
        QVERIFY(r.output.contains(".git/  (ignored, contents not listed)"));
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

    void gitignoreRules()
    {
        QTemporaryDir d;
        put(d.path() + "/.gitignore",
            "# comment\n*.log\n/out\ngen/\n!keep.log\ndocs/**/*.tmp\n**/cache\n");
        put(d.path() + "/sub/.gitignore", "local.txt\n");
        IgnoreRules ig(QFileInfo(d.path()).canonicalFilePath());
        QVERIFY(ig.isIgnored("a.log", false));
        QVERIFY(ig.isIgnored("deep/x/a.log", false));
        QVERIFY(!ig.isIgnored("keep.log", false));          // negated
        QVERIFY(ig.isIgnored("out", true));                  // anchored
        QVERIFY(!ig.isIgnored("src/out", true));
        QVERIFY(ig.isIgnored("x/gen", true));                // dir-only, any depth
        QVERIFY(!ig.isIgnored("x/gen", false));
        QVERIFY(ig.isIgnored("x/gen/file.cpp", false));      // inside an ignored folder
        QVERIFY(ig.isIgnored("docs/a/b/c.tmp", false));
        QVERIFY(!ig.isIgnored("docs/a/b/c.txt", false));
        QVERIFY(ig.isIgnored("a/b/cache", true));
        QVERIFY(ig.isIgnored("sub/local.txt", false));       // nested .gitignore
        QVERIFY(!ig.isIgnored("local.txt", false));
        QVERIFY(ig.isIgnored("node_modules", true));         // built-in noise
        QVERIFY(ig.isIgnored("build-release/x.o", false));
        QVERIFY(!ig.isIgnored("src/main.cpp", false));
    }

    void globs()
    {
        QVERIFY(IgnoreRules::globToRegex("*.cpp").match("main.cpp").hasMatch());
        QVERIFY(!IgnoreRules::globToRegex("*.cpp").match("src/main.cpp").hasMatch());
        QVERIFY(IgnoreRules::globToRegex("src/**/m*.cpp").match("src/a/b/main.cpp").hasMatch());
        QVERIFY(IgnoreRules::globToRegex("src/**/m*.cpp").match("src/main.cpp").hasMatch());
        QVERIFY(IgnoreRules::globToRegex("file?.[ch]").match("file1.h").hasMatch());
        QVERIFY(IgnoreRules::globToRegex("*.CPP").match("a.cpp").hasMatch());   // case-insensitive by default
    }

    void listRespectsGitignore()
    {
        QTemporaryDir d;
        put(d.path() + "/.gitignore", "*.log\nsecret/\n");
        put(d.path() + "/src/a.cpp", "x");
        put(d.path() + "/run.log", "x");
        put(d.path() + "/secret/key", "x");
        Workspace ws(d.path());
        AgentAction a = act(AgentAction::ListDir, ".");
        a.depth = 3;
        const ActionResult r = ws.listDir(a);
        QVERIFY(r.output.contains("src/a.cpp"));
        QVERIFY(!r.output.contains("run.log"));
        QVERIFY(r.output.contains("secret/  (ignored"));
        QVERIFY(!r.output.contains("secret/key"));
        QVERIFY2(r.status.contains("1 ignored files not shown"), qPrintable(r.status));
    }

    void searchFindsAndFilters()
    {
        QTemporaryDir d;
        put(d.path() + "/.gitignore", "gen/\n");
        put(d.path() + "/src/a.cpp", "int main() {\n    QTimer t;\n    qtimer();\n}\n");
        put(d.path() + "/src/b.h", "class QTimer;\n");
        put(d.path() + "/gen/c.cpp", "QTimer generated;\n");
        put(d.path() + "/bin.dat", QByteArray("QTimer\0\0", 8));
        Workspace ws(d.path());

        AgentAction s = act(AgentAction::Search, QString());
        s.query = "QTimer";
        ActionResult r = ws.search(s);
        QVERIFY(r.ok);
        QVERIFY2(r.output.contains("src/a.cpp:2: QTimer t;"), qPrintable(r.output));
        QVERIFY(r.output.contains("src/a.cpp:3: qtimer();"));        // case-insensitive by default
        QVERIFY(r.output.contains("src/b.h:1: class QTimer;"));
        QVERIFY(!r.output.contains("gen/"));                          // .gitignore
        QVERIFY(!r.output.contains("bin.dat"));                       // binary
        QVERIFY(r.status.contains("3 matches in 2 files"));

        s.caseSensitive = true;
        s.glob = "*.cpp";
        r = ws.search(s);
        QCOMPARE(r.output, QStringLiteral("src/a.cpp:2: QTimer t;"));

        s.glob.clear();
        s.caseSensitive = false;
        s.regex = true;
        s.query = "^class\\s+\\w+;";
        r = ws.search(s);
        QCOMPARE(r.output, QStringLiteral("src/b.h:1: class QTimer;"));

        s.query = "(";
        QVERIFY(!ws.search(s).ok);                                    // bad regex reported

        s.regex = false;
        s.query = "QTimer";
        s.maxResults = 1;
        r = ws.search(s);
        QCOMPARE(r.output.count('\n'), 0);
        QVERIFY(r.status.contains("stopped at 1"));

        s.maxResults = 0;
        s.path = "../";
        QVERIFY(!ws.search(s).ok);                                    // outside the project
    }

    void findFilesByNameAndPath()
    {
        QTemporaryDir d;
        put(d.path() + "/src/app/main.cpp", "x");
        put(d.path() + "/src/lib/util.cpp", "x");
        put(d.path() + "/tests/main_test.cpp", "x");
        put(d.path() + "/build/main.cpp", "x");                       // noise folder
        Workspace ws(d.path());
        AgentAction f = act(AgentAction::FindFiles, QString());
        f.glob = "main*";
        ActionResult r = ws.findFiles(f);
        QVERIFY(r.output.contains("src/app/main.cpp"));
        QVERIFY(r.output.contains("tests/main_test.cpp"));
        QVERIFY(!r.output.contains("build/"));
        f.glob = "src/**/*.cpp";
        r = ws.findFiles(f);
        QVERIFY(r.output.contains("src/lib/util.cpp") && !r.output.contains("tests/"));
    }

    void bigFilesComeInParts()
    {
        QTemporaryDir d;
        QByteArray big;
        for (int i = 1; i <= 1000; ++i)
            big += "line " + QByteArray::number(i) + "\n";
        put(d.path() + "/big.txt", big);
        Workspace ws(d.path());
        ActionResult r = ws.readFile(act(AgentAction::ReadFile, "big.txt"));
        QVERIFY(r.ok);
        QVERIFY(r.output.endsWith("line 400"));
        QVERIFY(r.status.contains("only lines 1-400"));
        AgentAction ranged = act(AgentAction::ReadFile, "big.txt");
        ranged.startLine = 990;
        r = ws.readFile(ranged);
        QVERIFY(r.output.startsWith("line 990"));
    }

    void notesFile()
    {
        QTemporaryDir d;
        Workspace ws(d.path());
        QVERIFY(ws.readNotes().isEmpty());
        AgentAction n;
        n.type = AgentAction::UpdateNotes;
        n.path = "../escape.md";                                      // ignored: always AGENT_NOTES.md
        n.content = "# Notes";
        QVERIFY(ws.writeNotes(n).ok);
        QCOMPARE(ws.readNotes(), QStringLiteral("# Notes\n"));
        QVERIFY(!QFile::exists(QFileInfo(d.path()).dir().filePath("escape.md")));
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
