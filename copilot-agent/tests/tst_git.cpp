#include "git.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

static void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

class TestGit : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString root() const { return m_dir.path(); }
    Git::Result git(const QStringList &args)
    {
        return Git::run(root(), QStringList{"-c", "user.name=Test", "-c", "user.email=test@example.com"} + args);
    }

private slots:
    void initTestCase()
    {
        if (!Git::run(QDir::tempPath(), {"--version"}).ok)
            QSKIP("git is not installed");
        QVERIFY(!Git::isRepo(root()));
        QVERIFY(git({"init", "-q", "-b", "main"}).ok || git({"init", "-q"}).ok);
        write(root() + "/a.txt", "one\ntwo\n");
        QVERIFY(git({"add", "."}).ok);
        QVERIFY(git({"commit", "-q", "-m", "first"}).ok);
        QVERIFY(Git::isRepo(root()));
    }

    void branches()
    {
        const QString base = Git::currentBranch(root());
        QVERIFY(base == "main" || base == "master");
        QCOMPARE(Git::defaultBase(root()), base);
        QVERIFY(Git::branchExists(root(), base));
        QVERIFY(git({"checkout", "-q", "-b", "PROJ-1-work"}).ok);
        QCOMPARE(Git::currentBranch(root()), QStringLiteral("PROJ-1-work"));
        write(root() + "/a.txt", "one\nTWO\n");
        QVERIFY(git({"commit", "-q", "-am", "second"}).ok);

        Git::Target branch;
        branch.kind = Git::Target::Branch;
        branch.base = base;
        QVERIFY(Git::diff(root(), branch).contains("+TWO"));
        QVERIFY(Git::diffStat(root(), branch).contains("a.txt"));

        Git::Target last;
        last.kind = Git::Target::LastCommit;
        QVERIFY(Git::diff(root(), last).contains("second"));
        QVERIFY(Git::diff(root(), last, "a.txt").contains("-two"));
    }

    void uncommitted()
    {
        write(root() + "/a.txt", "one\nTWO\nthree\n");
        write(root() + "/new/b.txt", "fresh");
        Git::Target t;   // Uncommitted
        const QString d = Git::diff(root(), t);
        QVERIFY2(d.contains("+three"), qPrintable(d));
        QCOMPARE(Git::untrackedFiles(root()), QStringList{"new/b.txt"});
        QString err;
        Git::diff(root(), t, "no/such/file", &err);
        QVERIFY(err.isEmpty());   // an unknown path is just an empty diff
    }

    void branchNames()
    {
        QCOMPARE(Git::branchNameFor("proj-12", "Fix the login timeout (again)!"), QStringLiteral("PROJ-12-fix-the-login-timeout-again"));
        QCOMPARE(Git::branchNameFor("AB-1", "Café à l'heure"), QStringLiteral("AB-1-cafe-a-l-heure"));
        QCOMPARE(Git::branchNameFor("AB-1", ""), QStringLiteral("AB-1"));
        const QString longName = Git::branchNameFor("AB-1", QString(40, 'x') + " " + QString(40, 'y'));
        QVERIFY(longName.size() <= 60 && longName.startsWith("AB-1-"));
    }
};

QTEST_GUILESS_MAIN(TestGit)
#include "tst_git.moc"
