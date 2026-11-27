#pragma once

#include <QString>
#include <QStringList>

// The few git operations the app runs itself (not Copilot): creating a
// branch for a Jira ticket, and producing the diff a review looks at. Each
// runs `git` in the project folder and waits for it (they are quick).
namespace Git {

struct Result {
    bool ok = false;
    int exitCode = -1;
    QString out;
    QString err;
};

Result run(const QString &root, const QStringList &args, int timeoutMs = 20000);

bool isRepo(const QString &root);
QString currentBranch(const QString &root);
bool branchExists(const QString &root, const QString &branch);
// "main", "master", or whatever origin/HEAD points at; empty if unknown.
QString defaultBase(const QString &root);

// What a review looks at.
struct Target {
    enum Kind { Uncommitted, LastCommit, Branch } kind = Uncommitted;
    QString base;   // Branch: compared as base...HEAD
    QString describe() const;
    QStringList diffArgs() const;   // after "git", before an optional "-- <path>"
};

QString diff(const QString &root, const Target &t, const QString &path = QString(), QString *error = nullptr);
QString diffStat(const QString &root, const Target &t, QString *error = nullptr);
QStringList untrackedFiles(const QString &root);

// "PROJ-123-fix-login-timeout": the key, then the summary's words, ASCII,
// lower case, at most 60 characters.
QString branchNameFor(const QString &issueKey, const QString &summary);

} // namespace Git
