#include "ignorerules.h"

#include <QFile>
#include <QSet>
#include <QStringList>

IgnoreRules::IgnoreRules(const QString &root)
    : m_root(root)
{
}

bool IgnoreRules::isNoiseDir(const QString &name)
{
    static const QSet<QString> noise = {
        QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral(".hg"),
        QStringLiteral("node_modules"), QStringLiteral(".venv"), QStringLiteral("venv"),
        QStringLiteral("__pycache__"), QStringLiteral(".idea"), QStringLiteral(".vs"),
        QStringLiteral(".vscode"), QStringLiteral(".cache"), QStringLiteral("dist"),
        QStringLiteral("target"), QStringLiteral(".gradle"), QStringLiteral(".qtcreator"),
    };
    return noise.contains(name) || name.startsWith(QLatin1String("build"))
        || name.startsWith(QLatin1String("cmake-build-"));
}

QRegularExpression IgnoreRules::globToRegex(const QString &glob, bool caseSensitive)
{
    QString re;
    re.reserve(glob.size() * 2);
    for (int i = 0; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (c == QLatin1Char('*')) {
            if (i + 1 < glob.size() && glob.at(i + 1) == QLatin1Char('*')) {
                ++i;
                if (i + 1 < glob.size() && glob.at(i + 1) == QLatin1Char('/')) {
                    ++i;
                    re += QStringLiteral("(?:.*/)?");   // "**/": any folders, or none
                } else {
                    re += QStringLiteral(".*");
                }
            } else {
                re += QStringLiteral("[^/]*");
            }
        } else if (c == QLatin1Char('?')) {
            re += QStringLiteral("[^/]");
        } else if (c == QLatin1Char('[')) {
            const int close = glob.indexOf(QLatin1Char(']'), i + 1);
            if (close < 0) {
                re += QStringLiteral("\\[");
            } else {
                QString cls = glob.mid(i + 1, close - i - 1);
                if (cls.startsWith(QLatin1Char('!')))
                    cls[0] = QLatin1Char('^');
                re += QLatin1Char('[') + cls + QLatin1Char(']');
                i = close;
            }
        } else if (c == QLatin1Char('\\') && i + 1 < glob.size()) {
            re += QRegularExpression::escape(QString(glob.at(++i)));
        } else {
            re += QRegularExpression::escape(QString(c));
        }
    }
    QRegularExpression::PatternOptions opts;
    if (!caseSensitive)
        opts |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(QStringLiteral("^") + re + QStringLiteral("$"), opts);
}

const QVector<IgnoreRules::Rule> &IgnoreRules::rulesFor(const QString &dirRel) const
{
    auto it = m_cache.find(dirRel);
    if (it != m_cache.end())
        return it.value();

    QVector<Rule> rules;
    QStringList files;
    const QString dirAbs = dirRel.isEmpty() ? m_root : m_root + QLatin1Char('/') + dirRel;
    files << dirAbs + QStringLiteral("/.gitignore");
    if (dirRel.isEmpty())
        files << m_root + QStringLiteral("/.git/info/exclude");
    for (const QString &path : files) {
        QFile f(path);
        if (m_root.isEmpty() || !f.open(QIODevice::ReadOnly))
            continue;
        const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
        for (QString line : lines) {
            line.remove(QLatin1Char('\r'));
            while (line.endsWith(QLatin1Char(' ')) && !line.endsWith(QLatin1String("\\ ")))
                line.chop(1);
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            Rule r;
            if (line.startsWith(QLatin1Char('!'))) {
                r.negate = true;
                line.remove(0, 1);
            } else if (line.startsWith(QLatin1String("\\!")) || line.startsWith(QLatin1String("\\#"))) {
                line.remove(0, 1);
            }
            if (line.endsWith(QLatin1Char('/'))) {
                r.dirOnly = true;
                line.chop(1);
            }
            r.anchored = line.contains(QLatin1Char('/'));
            if (line.startsWith(QLatin1Char('/')))
                line.remove(0, 1);
            if (line.isEmpty())
                continue;
            // Git compares case-sensitively on Linux; on Windows it ignores case.
#ifdef Q_OS_WIN
            r.re = globToRegex(line, false);
#else
            r.re = globToRegex(line, true);
#endif
            rules << r;
        }
    }
    return m_cache.insert(dirRel, rules).value();
}

bool IgnoreRules::matchHere(const QString &relPath, bool isDir) const
{
    const QString name = relPath.mid(relPath.lastIndexOf(QLatin1Char('/')) + 1);
    if (isDir && isNoiseDir(name))
        return true;

    // Rules from every .gitignore between the root and this entry; deeper
    // files come later, so they override.
    bool ignored = false;
    const QStringList parts = relPath.split(QLatin1Char('/'), QString::SkipEmptyParts);
    QString dirRel;
    for (int depth = 0; depth < parts.size(); ++depth) {
        const QString inDir = parts.mid(depth).join(QLatin1Char('/'));   // path relative to dirRel
        for (const Rule &r : rulesFor(dirRel)) {
            if (r.dirOnly && !isDir)
                continue;
            const bool hit = r.anchored ? r.re.match(inDir).hasMatch() : r.re.match(name).hasMatch();
            if (hit)
                ignored = !r.negate;
        }
        dirRel = dirRel.isEmpty() ? parts.at(depth) : dirRel + QLatin1Char('/') + parts.at(depth);
    }
    return ignored;
}

bool IgnoreRules::isIgnored(const QString &relPath, bool isDir) const
{
    QString rel = relPath;
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rel.isEmpty() || rel == QLatin1String("."))
        return false;
    // Anything inside an ignored folder is ignored, whatever its own rules say.
    const QStringList parts = rel.split(QLatin1Char('/'), QString::SkipEmptyParts);
    QString prefix;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        prefix = prefix.isEmpty() ? parts.at(i) : prefix + QLatin1Char('/') + parts.at(i);
        if (matchHere(prefix, true))
            return true;
    }
    return matchHere(rel, isDir);
}
