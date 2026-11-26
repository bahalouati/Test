#include "workspace.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <functional>
#include <QSet>

Workspace::Workspace(const QString &root)
{
    setRoot(root);
}

void Workspace::setRoot(const QString &root)
{
    m_root = root.isEmpty() ? QString() : QFileInfo(root).canonicalFilePath();
}

bool Workspace::isValid() const
{
    return !m_root.isEmpty() && QFileInfo(m_root).isDir();
}

static bool insideRoot(const QString &path, const QString &root)
{
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return path.compare(root, cs) == 0
        || path.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'), cs);
}

QString Workspace::resolve(const QString &relative, QString *error) const
{
    if (!isValid()) {
        *error = QStringLiteral("no project folder is open");
        return QString();
    }
    QString rel = relative.trimmed();
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rel.isEmpty())
        rel = QStringLiteral(".");
    if (QDir::isAbsolutePath(rel) || rel.startsWith(QLatin1Char('~'))) {
        *error = QStringLiteral("absolute paths are not allowed; use a path relative to the project root");
        return QString();
    }

    const QString abs = QDir::cleanPath(m_root + QLatin1Char('/') + rel);
    if (!insideRoot(abs, m_root)) {
        *error = QStringLiteral("path leaves the project folder");
        return QString();
    }

    // Walk up to the nearest part that exists and canonicalise it, so a
    // symlink inside the project cannot point the write somewhere else.
    QString probe = abs;
    while (!QFileInfo::exists(probe) && probe != m_root)
        probe = QFileInfo(probe).absolutePath();
    const QString canon = QFileInfo(probe).canonicalFilePath();
    if (canon.isEmpty() || !insideRoot(canon, m_root)) {
        *error = QStringLiteral("path resolves outside the project folder (symlink?)");
        return QString();
    }
    return abs;
}

static ActionResult fail(const AgentAction &a, const QString &why)
{
    ActionResult r;
    r.action = a;
    r.ok = false;
    r.status = QStringLiteral("error: ") + why;
    return r;
}

static bool readText(const QString &path, QString *out, QString *error)
{
    QFile f(path);
    if (f.size() > Workspace::kMaxReadBytes) {
        *error = QStringLiteral("file is %1 KB, over the %2 KB limit; read a line range instead")
                     .arg(f.size() / 1024).arg(Workspace::kMaxReadBytes / 1024);
        // Still allow a line range on big files: read it anyway.
    }
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return false;
    }
    const QByteArray bytes = f.readAll();
    if (bytes.left(8000).contains('\0')) {
        *error = QStringLiteral("binary file");
        return false;
    }
    *out = QString::fromUtf8(bytes);
    return true;
}

ActionResult Workspace::readFile(const AgentAction &a) const
{
    QString err;
    const QString abs = resolve(a.path, &err);
    if (abs.isEmpty())
        return fail(a, err);
    const QFileInfo fi(abs);
    if (!fi.exists())
        return fail(a, QStringLiteral("file not found"));
    if (fi.isDir())
        return fail(a, QStringLiteral("is a directory; use list_dir"));

    QString text;
    if (!readText(abs, &text, &err))
        return fail(a, err);
    const bool tooBig = fi.size() > kMaxReadBytes;
    if (tooBig && a.startLine == 0 && a.endLine == 0)
        return fail(a, err);

    const QStringList lines = text.split(QLatin1Char('\n'));
    const int total = int(lines.size());
    ActionResult r;
    r.action = a;
    r.ok = true;
    if (a.startLine > 0 || a.endLine > 0) {
        const int from = qBound(1, a.startLine > 0 ? a.startLine : 1, total);
        const int to = qBound(from, a.endLine > 0 ? a.endLine : total, total);
        r.output = lines.mid(from - 1, to - from + 1).join(QLatin1Char('\n'));
        r.status = QStringLiteral("ok, lines %1-%2 of %3").arg(from).arg(to).arg(total);
    } else {
        r.output = text;
        r.status = QStringLiteral("ok, %1 lines").arg(total);
    }
    return r;
}

ActionResult Workspace::listDir(const AgentAction &a) const
{
    QString err;
    const QString abs = resolve(a.path, &err);
    if (abs.isEmpty())
        return fail(a, err);
    if (!QFileInfo(abs).isDir())
        return fail(a, QStringLiteral("not a directory"));

    static const QSet<QString> skip = {
        QStringLiteral(".git"), QStringLiteral("node_modules"), QStringLiteral(".venv"),
        QStringLiteral("__pycache__"), QStringLiteral(".idea"), QStringLiteral(".vs"),
        QStringLiteral(".cache"), QStringLiteral("dist"), QStringLiteral("target")};

    QStringList entries;
    bool truncated = false;
    const QDir base(abs);
    std::function<void(const QString &, int)> walk = [&](const QString &dir, int level) {
        QDir d(dir);
        const QFileInfoList list = d.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo &fi : list) {
            if (entries.size() >= kMaxListEntries) {
                truncated = true;
                return;
            }
            const QString rel = base.relativeFilePath(fi.absoluteFilePath());
            if (fi.isDir()) {
                const bool skipped = skip.contains(fi.fileName())
                                  || fi.fileName().startsWith(QLatin1String("build"));
                entries << rel + (skipped ? QStringLiteral("/  (contents not listed)") : QStringLiteral("/"));
                if (!skipped && !fi.isSymLink() && level + 1 < a.depth)
                    walk(fi.absoluteFilePath(), level + 1);
            } else {
                entries << QStringLiteral("%1  (%2 bytes)").arg(rel).arg(fi.size());
            }
        }
    };
    walk(abs, 0);

    ActionResult r;
    r.action = a;
    r.ok = true;
    r.status = QStringLiteral("ok, %1 entries%2").arg(entries.size())
                   .arg(truncated ? QStringLiteral(" (truncated)") : QString());
    r.output = entries.isEmpty() ? QStringLiteral("(empty)") : entries.join(QLatin1Char('\n'));
    return r;
}

static bool saveText(const QString &abs, const QString &text, QString *error)
{
    if (!QDir().mkpath(QFileInfo(abs).absolutePath())) {
        *error = QStringLiteral("cannot create the parent folder");
        return false;
    }
    QSaveFile f(abs);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = f.errorString();
        return false;
    }
    f.write(text.toUtf8());
    if (!f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}

ActionResult Workspace::writeFile(const AgentAction &a) const
{
    QString err;
    const QString abs = resolve(a.path, &err);
    if (abs.isEmpty())
        return fail(a, err);
    if (QFileInfo(abs).isDir())
        return fail(a, QStringLiteral("is a directory"));
    const bool existed = QFileInfo::exists(abs);
    if (!saveText(abs, a.content, &err))
        return fail(a, err);
    ActionResult r;
    r.action = a;
    r.ok = true;
    r.status = QStringLiteral("%1, %2 lines written")
                   .arg(existed ? QStringLiteral("updated") : QStringLiteral("created"))
                   .arg(a.content.count(QLatin1Char('\n')) + (a.content.endsWith(QLatin1Char('\n')) ? 0 : 1));
    return r;
}

QString Workspace::currentContent(const QString &relative, bool *exists) const
{
    QString err, text;
    const QString abs = resolve(relative, &err);
    *exists = !abs.isEmpty() && QFileInfo(abs).isFile();
    if (*exists)
        readText(abs, &text, &err);
    return text;
}

QString Workspace::applyReplace(const AgentAction &a, QString *error) const
{
    const QString abs = resolve(a.path, error);
    if (abs.isEmpty())
        return QString();
    if (!QFileInfo(abs).isFile()) {
        *error = QStringLiteral("file not found");
        return QString();
    }
    QString text;
    if (!readText(abs, &text, error))
        return QString();

    // Models often normalise line endings; match on \n and restore CRLF after.
    const bool crlf = text.contains(QLatin1String("\r\n"));
    QString body = text, oldText = a.oldText, newText = a.newText;
    if (crlf) {
        body.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        oldText.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        newText.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    }
    const int count = int(body.count(oldText));
    if (count == 0) {
        *error = QStringLiteral("\"old\" text not found in the file; read the file again and copy it exactly");
        return QString();
    }
    if (count > 1) {
        *error = QStringLiteral("\"old\" text occurs %1 times; include more surrounding lines so it is unique").arg(count);
        return QString();
    }
    body.replace(body.indexOf(oldText), oldText.size(), newText);
    if (crlf)
        body.replace(QLatin1String("\n"), QLatin1String("\r\n"));
    error->clear();
    return body;
}

ActionResult Workspace::replaceInFile(const AgentAction &a) const
{
    QString err;
    const QString updated = applyReplace(a, &err);
    if (!err.isEmpty())
        return fail(a, err);
    const QString abs = resolve(a.path, &err);
    if (!saveText(abs, updated, &err))
        return fail(a, err);
    ActionResult r;
    r.action = a;
    r.ok = true;
    r.status = QStringLiteral("ok, 1 replacement");
    return r;
}
