#pragma once

#include "agentaction.h"
#include "ignorerules.h"

#include <QString>

#include <functional>

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
    ActionResult setCells(const AgentAction &a) const;

    // Word/Excel edits (replace_in_file on .docx, set_cells on .xlsx):
    // the new file bytes and the readable text before and after, for the
    // approval preview. Nothing is written.
    bool officeEdit(const AgentAction &a, QByteArray *newBytes, QString *before, QString *after,
                    QString *error, QString *note) const;
    static constexpr qint64 kMaxOfficeBytes = 50 * 1024 * 1024;
    // Read-only and safe to run off the GUI thread (on a copy).
    ActionResult search(const AgentAction &a) const;
    ActionResult findFiles(const AgentAction &a) const;

    // The project notes Copilot keeps between tasks.
    static QString notesFileName() { return QStringLiteral("AGENT_NOTES.md"); }
    QString readNotes() const;                 // empty when there are none
    ActionResult writeNotes(const AgentAction &a) const;

    bool isIgnored(const QString &relPath, bool isDir) const { return m_ignore.isIgnored(relPath, isDir); }

    // What the file holds now; empty (and *exists false) for a new file.
    // Used to build the diff shown in the approval dialog.
    QString currentContent(const QString &relative, bool *exists) const;

    // For replace_in_file: the whole file after the edit, or empty with
    // *error set when "old" does not occur exactly once.
    QString applyReplace(const AgentAction &a, QString *error) const;

    static constexpr qint64 kMaxReadBytes = 512 * 1024;
    static constexpr int kMaxListEntries = 600;
    // read_file without a range on a file longer than this returns only
    // the first kHeadLines lines, so a big file does not flood the chat.
    static constexpr int kBigFileLines = 600;
    static constexpr int kHeadLines = 400;
    static constexpr int kDefaultMatches = 100;
    static constexpr int kMaxMatches = 300;
    static constexpr qint64 kMaxSearchFileBytes = 2 * 1024 * 1024;
    static constexpr int kSearchTimeMs = 15000;

private:
    QString m_root;   // canonical
    IgnoreRules m_ignore;
    // Calls visit(absPath, relPath) for every file under startAbs that is
    // not ignored, until visit returns false or the time budget runs out.
    bool walkFiles(const QString &startAbs, const std::function<bool(const QString &, const QString &)> &visit) const;
};
