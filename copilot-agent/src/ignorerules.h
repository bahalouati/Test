#pragma once

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QVector>

// Which files the agent should not bother with: the project's .gitignore
// files (root and nested, plus .git/info/exclude) and a few folders that
// are never source (.git, node_modules, build output, virtualenvs...).
//
// Covers the common .gitignore forms: comments, blank lines, "!" negation,
// trailing "/" (folders only), leading or inner "/" (anchored to the
// .gitignore's folder), "*", "?", "**" and [...] classes. The last matching
// rule wins, and anything inside an ignored folder is ignored.
class IgnoreRules
{
public:
    explicit IgnoreRules(const QString &root = QString());

    // relPath uses forward slashes, relative to the project root.
    bool isIgnored(const QString &relPath, bool isDir) const;

    // Folders skipped even without a .gitignore.
    static bool isNoiseDir(const QString &name);

    // A glob as used by find_files/search filters: "*" stays within a
    // folder, "**" crosses folders, "?" is one character.
    static QRegularExpression globToRegex(const QString &glob, bool caseSensitive = false);

private:
    struct Rule {
        QRegularExpression re;
        bool negate = false;
        bool dirOnly = false;
        bool anchored = false;   // match the path relative to the rule's folder, not just the name
    };
    const QVector<Rule> &rulesFor(const QString &dirRel) const;
    bool matchHere(const QString &relPath, bool isDir) const;   // ignoring ancestors

    QString m_root;
    mutable QHash<QString, QVector<Rule>> m_cache;   // folder (relative) -> its .gitignore rules
};
