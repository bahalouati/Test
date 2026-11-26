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
QString taskPrompt(const QString &task, const QString &workspaceName,
                   const QString &platform, const QString &shell);

// Parses the text of rendered code blocks. Blocks that are not agent JSON
// (ordinary code Copilot shows while explaining) are skipped silently; blocks
// that look like an action but are broken come back as Invalid so Copilot
// hears about the mistake.
QList<AgentAction> parseBlocks(const QStringList &blocks);

// Fallback for when the page shows raw markdown instead of rendered blocks:
// pulls the bodies of ```agent / ```json fences out of the text.
QStringList extractFencedBlocks(const QString &markdown);

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

} // namespace AgentProtocol
