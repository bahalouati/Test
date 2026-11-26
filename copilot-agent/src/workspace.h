#pragma once

#include "agentaction.h"

#include <QString>

// The project folder the agent works in. Every path Copilot sends is resolved
// here, and anything that would land outside the root (.., absolute paths,
// symlinks pointing out) is refused before any file is touched.
class Workspace
{
public:
    explicit Workspace(const QString &root = QString());

    void setRoot(const QString &root);
    QString root() const { return m_root; }
    bool isValid() const;

    // Absolute path inside the root, or empty with *error set.
    QString resolve(const QString &relative, QString *error) const;

    ActionResult readFile(const AgentAction &a) const;
    ActionResult listDir(const AgentAction &a) const;
    ActionResult writeFile(const AgentAction &a) const;
    ActionResult replaceInFile(const AgentAction &a) const;

    // What the file holds now; empty (and *exists false) for a new file.
    // Used to build the diff shown in the approval dialog.
    QString currentContent(const QString &relative, bool *exists) const;

    // For replace_in_file: the whole file after the edit, or empty with
    // *error set when "old" does not occur exactly once.
    QString applyReplace(const AgentAction &a, QString *error) const;

    static constexpr qint64 kMaxReadBytes = 512 * 1024;
    static constexpr int kMaxListEntries = 600;

private:
    QString m_root;   // canonical
};
