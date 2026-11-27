#pragma once

#include "agentaction.h"

#include <QList>
#include <QString>
#include <QStringList>

// The text contract between the app and Copilot: the instructions that turn a
// chat into an agent, the parser for its ```agent blocks, and the format the
// results go back in.
namespace AgentProtocol {

// The first message of a task: how to ask for actions, then the task itself.
// notes: the project's AGENT_NOTES.md, if any, so a new conversation starts
// with what earlier ones learned.
// focus: from focusSection(), the files the user picked for this task.
// notesAllowed: false when the user limited changes to those files.
QString taskPrompt(const QString &task, const QString &workspaceName,
                   const QString &platform, const QString &shell, const QString &notes = QString(),
                   const QString &focus = QString(), bool notesAllowed = true);

// The part of the first message about files the user picked by hand:
// which they are, the content of those that fit (path -> text), the names of
// those too long to include, and whether only they may be changed.
struct FocusFile { QString path; QString content; int lines = 0; bool included = false; };
QString focusSection(const QList<FocusFile> &files, bool onlyThese);

// Parses the text of rendered code blocks. Blocks that are not agent JSON
// (ordinary code Copilot shows while explaining) are skipped silently; blocks
// that look like an action but are broken come back as Invalid so Copilot
// hears about the mistake.
QList<AgentAction> parseBlocks(const QStringList &blocks);

// Pulls the bodies of all ``` fences out of markdown (any language label;
// parseBlocks() later drops the ones that are not actions).
QStringList extractFencedBlocks(const QString &markdown);

// Top-level {...} objects in free text, honouring JSON string escapes. For
// replies copied without their fences.
QStringList extractJsonObjects(const QString &text);

// A reply copied by hand from Copilot (its copy button gives markdown, a
// plain selection gives text): the candidate blocks for parseBlocks().
QStringList blocksFromReplyText(const QString &text);

// The message that reports a step's results back to Copilot.
QString formatResults(const QList<ActionResult> &results, int step, int charLimit);

// Wraps body in a fence long enough that backticks inside cannot close it.
QString fence(const QString &body, const QString &lang = QString());

// Keeps the head and the tail, which is where compiler errors and test
// summaries usually are, and says how much was cut.
QString truncateMiddle(const QString &text, int limit);

// Whitespace-insensitive form used to recognise blocks we sent ourselves.
QString normalized(const QString &text);

QString actionLabel(const AgentAction &a);

// Copilot can get things wrong. These look for the usual ways and return
// one human-readable warning per problem, shown in the approval dialog.
//
// write_file: placeholders such as "// ... rest of the file unchanged ..."
// that would replace real code, a file that shrinks drastically, or an
// existing file being emptied.
QStringList writeWarnings(const QString &oldText, bool existed, const QString &newText);
// run_command: deleting recursively, history-rewriting git commands,
// admin rights, piping a download into a shell, and the like.
QStringList commandWarnings(const QString &command);

// Identifies a step by its actions and their results, to notice Copilot
// repeating itself.
QByteArray stepSignature(const QList<ActionResult> &results);

} // namespace AgentProtocol
