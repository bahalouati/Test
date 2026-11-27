#include "git.h"

#include <QProcess>
#include <QRegularExpression>

namespace Git {

Result run(const QString &root, const QStringList &args, int timeoutMs)
{
    Result r;
    QProcess p;
    p.setWorkingDirectory(root);
    p.setStandardInputFile(QProcess::nullDevice());
    // Plain, untranslated, uncoloured output; never open a pager or editor.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("git"), QStringList{QStringLiteral("-c"), QStringLiteral("color.ui=false"),
                                               QStringLiteral("-c"), QStringLiteral("core.quotepath=false")} + args);
    if (!p.waitForStarted(5000)) {
        r.err = QStringLiteral("git is not installed or not on PATH");
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        r.err = QStringLiteral("git took too long");
        return r;
    }
    r.exitCode = p.exitCode();
    r.out = QString::fromUtf8(p.readAllStandardOutput());
    r.err = QString::fromUtf8(p.readAllStandardError()).trimmed();
    r.ok = p.exitStatus() == QProcess::NormalExit && r.exitCode == 0;
    return r;
}

bool isRepo(const QString &root)
{
    const Result r = run(root, {QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")});
    return r.ok && r.out.trimmed() == QLatin1String("true");
}

QString currentBranch(const QString &root)
{
    const Result r = run(root, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD")});
    return r.ok ? r.out.trimmed() : QString();
}

bool branchExists(const QString &root, const QString &branch)
{
    return run(root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
                      QStringLiteral("refs/heads/") + branch}).ok;
}

QString defaultBase(const QString &root)
{
    const Result head = run(root, {QStringLiteral("symbolic-ref"), QStringLiteral("--quiet"), QStringLiteral("--short"),
                                   QStringLiteral("refs/remotes/origin/HEAD")});
    if (head.ok && !head.out.trimmed().isEmpty())
        return head.out.trimmed();                       // e.g. origin/main
    for (const QString &name : {QStringLiteral("main"), QStringLiteral("master"), QStringLiteral("develop")})
        if (branchExists(root, name))
            return name;
    return QString();
}

QString Target::describe() const
{
    switch (kind) {
    case Uncommitted: return QStringLiteral("the uncommitted changes (staged and not staged) against the last commit");
    case LastCommit:  return QStringLiteral("the last commit");
    case Branch:      return QStringLiteral("this branch's changes since it left %1").arg(base);
    }
    return QString();
}

QStringList Target::diffArgs() const
{
    switch (kind) {
    case Uncommitted: return {QStringLiteral("diff"), QStringLiteral("HEAD")};
    case LastCommit:  return {QStringLiteral("show"), QStringLiteral("--format=%h %s%n"), QStringLiteral("HEAD")};
    case Branch:      return {QStringLiteral("diff"), base + QStringLiteral("...HEAD")};
    }
    return {};
}

QString diff(const QString &root, const Target &t, const QString &path, QString *error)
{
    QStringList args = t.diffArgs();
    args << QStringLiteral("--no-ext-diff") << QStringLiteral("-U5");
    if (!path.isEmpty())
        args << QStringLiteral("--") << path;
    const Result r = run(root, args);
    if (!r.ok && error)
        *error = r.err.isEmpty() ? QStringLiteral("git failed") : r.err;
    return r.out;
}

QString diffStat(const QString &root, const Target &t, QString *error)
{
    QStringList args = t.diffArgs();
    args << QStringLiteral("--stat=120");
    const Result r = run(root, args);
    if (!r.ok && error)
        *error = r.err.isEmpty() ? QStringLiteral("git failed") : r.err;
    return r.out.trimmed();
}

QStringList untrackedFiles(const QString &root)
{
    const Result r = run(root, {QStringLiteral("ls-files"), QStringLiteral("--others"), QStringLiteral("--exclude-standard")});
    return r.ok ? r.out.split(QLatin1Char('\n'), QString::SkipEmptyParts) : QStringList();
}

QString branchNameFor(const QString &issueKey, const QString &summary)
{
    QString slug = summary.normalized(QString::NormalizationForm_KD).toLower();
    slug.remove(QRegularExpression(QStringLiteral("[^\\x00-\\x7f]")));      // accents fall away after NFKD
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    QString name = issueKey.trimmed().toUpper();
    if (!slug.isEmpty())
        name += QLatin1Char('-') + slug;
    if (name.size() > 60) {
        name.truncate(60);
        const int cut = name.lastIndexOf(QLatin1Char('-'));
        if (cut > issueKey.size())
            name.truncate(cut);
    }
    return name;
}

} // namespace Git
