#include "agentprotocol.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

namespace AgentProtocol {

QString taskPrompt(const QString &task, const QString &workspaceName,
                   const QString &platform, const QString &shell)
{
    // Written as an ordinary request from a person, not as a "system prompt":
    // no role assignment, no ALL-CAPS rules, no "never/ignore" wording, which
    // chat safety filters can read as an injection attempt.
    //
    // Examples are inline code, not fenced blocks: the bridge only reads
    // fenced blocks, so our own message can never be mistaken for a reply.
    return QStringLiteral(
        "Hi! I'd like your help with a programming task in a project on my computer.\n"
        "\n"
        "I use a small helper app that can carry out your suggestions for me: it can show you files, "
        "save the changes you propose, and run commands, and then it pastes the results back here. "
        "I review and approve every file change and every command before it happens.\n"
        "\n"
        "So the app can understand your suggestions, please put each step in its own code block with "
        "the language set to agent, containing one JSON object. These are the steps it understands "
        "(replace the <...> parts with real values):\n"
        "- list a folder: `{\"action\":\"list_dir\",\"path\":\"<folder>\",\"depth\":<levels>}`\n"
        "- show a file: `{\"action\":\"read_file\",\"path\":\"<file>\"}` (\"start_line\" and \"end_line\" are optional)\n"
        "- save a whole file: `{\"action\":\"write_file\",\"path\":\"<file>\",\"content\":\"<the full file content>\"}`\n"
        "- change part of a file: `{\"action\":\"replace_in_file\",\"path\":\"<file>\",\"old\":\"<exact current text>\",\"new\":\"<new text>\"}`\n"
        "- run a command: `{\"action\":\"run_command\",\"command\":\"<command>\",\"cwd\":\"<folder>\",\"timeout\":<seconds>}`\n"
        "- finish: `{\"action\":\"done\",\"summary\":\"<what changed and how to check it>\"}`\n"
        "\n"
        "A few things that help:\n"
        "- Paths are relative to the project folder, with forward slashes.\n"
        "- The JSON needs to be valid, so newlines in strings are written as \\n and quotes as \\\".\n"
        "- It works best to look at a file before changing it. For small edits, replace_in_file is easiest; "
        "its \"old\" text should match the file exactly and appear only once. write_file replaces the whole file.\n"
        "- After a set of steps, please wait for me to paste the results before going on.\n"
        "- Please keep commands simple and safe: building, testing and inspecting are great. Please avoid "
        "deleting things, changing system settings or anything that needs administrator rights, and avoid "
        "commands that wait for input or keep running.\n"
        "- If you need a decision from me, just ask in plain text.\n"
        "- When everything is done and checked, send the finish step.\n"
        "\n"
        "About my setup: the project folder is %1, the operating system is %2, and commands run in %3.\n"
        "\n"
        "Here is the task:\n%4\n"
        "\n"
        "Could you start by having a look at the project?")
        .arg(workspaceName, platform, shell, task.trimmed());
}

static QString str(const QJsonObject &o, std::initializer_list<const char *> keys)
{
    for (const char *k : keys) {
        const QJsonValue v = o.value(QLatin1String(k));
        if (v.isString())
            return v.toString();
    }
    return QString();
}

static bool has(const QJsonObject &o, std::initializer_list<const char *> keys)
{
    for (const char *k : keys)
        if (o.value(QLatin1String(k)).isString())
            return true;
    return false;
}

static int num(const QJsonObject &o, const char *key, int fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isDouble())
        return v.toInt();
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().toInt(&ok);
        if (ok)
            return n;
    }
    return fallback;
}

static AgentAction fromObject(const QJsonObject &o)
{
    AgentAction a;
    a.raw = o;
    a.name = str(o, {"action", "tool", "type"}).trimmed().toLower();
    a.path = str(o, {"path", "file", "filename"}).trimmed();
    if (a.path.startsWith(QLatin1String("./")))
        a.path = a.path.mid(2);

    const QString n = a.name;
    if (n == QLatin1String("read_file") || n == QLatin1String("read")) {
        a.type = AgentAction::ReadFile;
        a.startLine = qMax(0, num(o, "start_line", 0));
        a.endLine = qMax(0, num(o, "end_line", 0));
        if (a.path.isEmpty())
            a.error = QStringLiteral("read_file needs \"path\"");
    } else if (n == QLatin1String("list_dir") || n == QLatin1String("list") || n == QLatin1String("ls")) {
        a.type = AgentAction::ListDir;
        if (a.path.isEmpty())
            a.path = QStringLiteral(".");
        a.depth = qBound(1, num(o, "depth", 1), 6);
    } else if (n == QLatin1String("write_file") || n == QLatin1String("write") || n == QLatin1String("create_file")) {
        a.type = AgentAction::WriteFile;
        a.content = str(o, {"content", "text", "contents"});
        if (a.path.isEmpty())
            a.error = QStringLiteral("write_file needs \"path\"");
        else if (!has(o, {"content", "text", "contents"}))
            a.error = QStringLiteral("write_file needs \"content\"");
    } else if (n == QLatin1String("replace_in_file") || n == QLatin1String("edit_file") || n == QLatin1String("replace")) {
        a.type = AgentAction::ReplaceInFile;
        a.oldText = str(o, {"old", "old_text", "search", "find"});
        a.newText = str(o, {"new", "new_text", "replace", "replacement"});
        if (a.path.isEmpty())
            a.error = QStringLiteral("replace_in_file needs \"path\"");
        else if (a.oldText.isEmpty())
            a.error = QStringLiteral("replace_in_file needs a non-empty \"old\"");
        else if (!has(o, {"new", "new_text", "replace", "replacement"}))
            a.error = QStringLiteral("replace_in_file needs \"new\" (use \"\" to delete)");
    } else if (n == QLatin1String("run_command") || n == QLatin1String("run") || n == QLatin1String("shell")) {
        a.type = AgentAction::RunCommand;
        a.command = str(o, {"command", "cmd"}).trimmed();
        a.cwd = str(o, {"cwd", "dir", "workdir"}).trimmed();
        a.timeoutSec = qMax(0, num(o, "timeout", 0));
        if (a.command.isEmpty())
            a.error = QStringLiteral("run_command needs \"command\"");
    } else if (n == QLatin1String("done") || n == QLatin1String("finish") || n == QLatin1String("complete")) {
        a.type = AgentAction::Done;
        a.summary = str(o, {"summary", "message", "result"});
    } else {
        a.error = QStringLiteral("unknown action \"%1\"").arg(a.name);
    }
    if (!a.error.isEmpty())
        a.type = AgentAction::Invalid;
    return a;
}

// Copilot sometimes leaks the fence's language label into the block's text
// ("agent\n{...}") or wraps the JSON in prose; find the JSON itself.
static QString jsonCandidate(const QString &block)
{
    QString t = block.trimmed();
    static const QRegularExpression label(QStringLiteral("^(agent|json|jsonc)\\s*\\n"),
                                          QRegularExpression::CaseInsensitiveOption);
    t.remove(label);
    t = t.trimmed();
    if (t.startsWith(QLatin1Char('{')) || t.startsWith(QLatin1Char('[')))
        return t;
    const int a = t.indexOf(QLatin1Char('{'));
    const int b = t.lastIndexOf(QLatin1Char('}'));
    if (a >= 0 && b > a)
        return t.mid(a, b - a + 1);
    return QString();
}

static bool looksLikeAction(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("\"(action|tool)\"\\s*:"));
    return re.match(text).hasMatch();
}

QList<AgentAction> parseBlocks(const QStringList &blocks)
{
    QList<AgentAction> out;
    for (const QString &block : blocks) {
        const QString json = jsonCandidate(block);
        if (json.isEmpty())
            continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError) {
            if (looksLikeAction(json)) {
                AgentAction bad;
                bad.error = QStringLiteral("invalid JSON (%1 at offset %2)")
                                .arg(err.errorString()).arg(err.offset);
                bad.name = QStringLiteral("?");
                out.append(bad);
            }
            continue;
        }

        QJsonArray items;
        if (doc.isArray()) {
            items = doc.array();
        } else {
            const QJsonObject o = doc.object();
            if (o.value(QLatin1String("actions")).isArray())
                items = o.value(QLatin1String("actions")).toArray();
            else
                items.append(o);
        }
        for (const QJsonValue &v : items) {
            if (!v.isObject())
                continue;
            const QJsonObject o = v.toObject();
            if (!o.contains(QLatin1String("action")) && !o.contains(QLatin1String("tool")))
                continue; // ordinary JSON shown as an example
            out.append(fromObject(o));
        }
    }
    return out;
}

QStringList extractFencedBlocks(const QString &markdown)
{
    QStringList out;
    static const QRegularExpression re(
        QStringLiteral("(`{3,})[ \\t]*([\\w+-]*)[ \\t]*\\r?\\n(.*?)\\r?\\n[ \\t]*\\1"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(markdown);
    while (it.hasNext())
        out.append(it.next().captured(3));
    return out;
}

QStringList extractJsonObjects(const QString &text)
{
    QStringList out;
    int depth = 0, start = -1;
    bool inString = false, escaped = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inString) {
            if (escaped) escaped = false;
            else if (c == QLatin1Char('\\')) escaped = true;
            else if (c == QLatin1Char('"')) inString = false;
            continue;
        }
        if (c == QLatin1Char('"') && depth > 0) {
            inString = true;
        } else if (c == QLatin1Char('{')) {
            if (depth++ == 0)
                start = i;
        } else if (c == QLatin1Char('}') && depth > 0) {
            if (--depth == 0)
                out << text.mid(start, i - start + 1);
        }
    }
    return out;
}

QStringList blocksFromReplyText(const QString &text)
{
    const QStringList fenced = extractFencedBlocks(text);
    if (!parseBlocks(fenced).isEmpty())
        return fenced;
    return extractJsonObjects(text);
}

QString fence(const QString &body, const QString &lang)
{
    int longest = 0, run = 0;
    for (const QChar c : body) {
        run = (c == QLatin1Char('`')) ? run + 1 : 0;
        longest = qMax(longest, run);
    }
    const QString f(qMax(3, longest + 1), QLatin1Char('`'));
    QString b = body;
    if (!b.endsWith(QLatin1Char('\n')))
        b += QLatin1Char('\n');
    return f + lang + QLatin1Char('\n') + b + f;
}

QString truncateMiddle(const QString &text, int limit)
{
    if (limit <= 0 || text.size() <= limit)
        return text;
    const int head = limit * 2 / 5;
    const int tail = limit - head;
    const int cut = text.size() - head - tail;
    return text.left(head)
         + QStringLiteral("\n... [%1 characters omitted] ...\n").arg(cut)
         + text.right(tail);
}

QString normalized(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text)
        if (!c.isSpace())
            out.append(c);
    return out;
}

static int lineCount(const QString &t)
{
    if (t.isEmpty())
        return 0;
    return int(t.count(QLatin1Char('\n'))) + (t.endsWith(QLatin1Char('\n')) ? 0 : 1);
}

QStringList writeWarnings(const QString &oldText, bool existed, const QString &newText)
{
    QStringList w;
    static const QRegularExpression placeholders[] = {
        QRegularExpression(QStringLiteral("\\.\\.\\.\\s*\\(?\\s*(rest|remaining|existing|other|previous|unchanged|same|more)\\b"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("\\b(rest|remainder) of (the )?(file|code|class|function|method|implementation)\\b"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("\\b(existing|unchanged|previous|original) (code|content|implementation)( (here|remains|stays|unchanged))?\\b"),
                           QRegularExpression::CaseInsensitiveOption),
        // A line that is nothing but "..." (optionally commented).
        QRegularExpression(QStringLiteral("^\\s*(//|#|--|/\\*|<!--)?\\s*(\\.\\.\\.|\u2026)\\s*(\\*/|-->)?\\s*$"),
                           QRegularExpression::MultilineOption),
    };
    for (const QRegularExpression &re : placeholders) {
        const QRegularExpressionMatch m = re.match(newText);
        // Only new placeholders count: the file may legitimately contain "...".
        if (m.hasMatch() && !oldText.contains(m.captured(0))) {
            w << QStringLiteral("The new content contains \"%1\", which looks like a placeholder for code "
                                "Copilot left out. Applying it would delete that code.")
                     .arg(m.captured(0).trimmed().left(80));
            break;
        }
    }
    if (existed) {
        const int before = lineCount(oldText), after = lineCount(newText);
        if (after == 0 && before > 0)
            w << QStringLiteral("This empties a file that has %1 lines.").arg(before);
        else if (before >= 30 && after * 2 < before)
            w << QStringLiteral("The file shrinks from %1 to %2 lines. Check that nothing was cut off by mistake.")
                     .arg(before).arg(after);
    }
    return w;
}

QStringList commandWarnings(const QString &command)
{
    struct Rule { const char *pattern; const char *why; };
    static const Rule rules[] = {
        {"\\brm\\s+(-\\w*\\s+)*-\\w*[rRf]", "deletes files (recursively or forced)"},
        {"\\b(rmdir|rd)\\s+(/\\w\\s+)*/s\\b", "deletes a folder tree"},
        {"\\b(del|erase)\\b.*\\s/[sq]\\b", "deletes files without asking"},
        {"\\bRemove-Item\\b.*-Recurse", "deletes a folder tree"},
        {"\\bformat\\s+[a-z]:", "formats a drive"},
        {"\\b(mkfs|diskpart|fdisk)\\b|\\bdd\\s+if=", "writes to disks directly"},
        {"\\bgit\\s+(reset\\s+--hard|clean\\s+-\\w*f|checkout\\s+--?\\s*\\.|restore\\s+\\.)", "throws away uncommitted work"},
        {"\\bgit\\s+push\\b.*(--force\\b|\\s-f\\b|--force-with-lease)", "rewrites history on the remote"},
        {"\\bgit\\s+push\\b", "publishes commits to the remote"},
        {"\\b(sudo|su|runas|doas)\\b|Start-Process\\b.*-Verb\\s+RunAs", "asks for administrator rights"},
        {"\\b(shutdown|reboot|halt|poweroff)\\b|Restart-Computer|Stop-Computer", "shuts down or restarts the computer"},
        {"\\breg\\s+(add|delete|import)\\b|Set-ItemProperty\\b.*HK(LM|CU)", "changes the Windows registry"},
        {"(curl|wget|iwr|Invoke-WebRequest|irm|Invoke-RestMethod)\\b[^|]*\\|\\s*(sh|bash|zsh|iex|Invoke-Expression|powershell|pwsh|python)\\b",
         "downloads something and runs it straight away"},
        {"\\b(chmod|icacls|takeown)\\b.*(-R|/t)\\b", "changes permissions recursively"},
        {"\\b(npm|pip|pip3|choco|winget|apt|apt-get|brew)\\s+(install|i)\\b.*\\s(-g|--global|--user)\\b|\\b(choco|winget|apt|apt-get|brew)\\s+install\\b",
         "installs software system-wide"},
        {"\\bsetx\\b|\\[Environment\\]::SetEnvironmentVariable", "changes environment variables permanently"},
    };
    QStringList w;
    for (const Rule &r : rules) {
        const QRegularExpression re(QString::fromLatin1(r.pattern), QRegularExpression::CaseInsensitiveOption);
        if (re.match(command).hasMatch()) {
            const QString why = QString::fromLatin1(r.why);
            if (!w.contains(why))
                w << why;
        }
    }
    for (QString &s : w)
        s = QStringLiteral("This command ") + s + QLatin1Char('.');
    return w;
}

QByteArray stepSignature(const QList<ActionResult> &results)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    for (const ActionResult &r : results) {
        h.addData(QJsonDocument(r.action.raw).toJson(QJsonDocument::Compact));
        h.addData(r.status.toUtf8());
        h.addData(r.output.toUtf8());
        h.addData("\x1e", 1);
    }
    return h.result();
}

QString actionLabel(const AgentAction &a)
{
    switch (a.type) {
    case AgentAction::ReadFile:      return QStringLiteral("read_file %1").arg(a.path);
    case AgentAction::ListDir:       return QStringLiteral("list_dir %1").arg(a.path);
    case AgentAction::WriteFile:     return QStringLiteral("write_file %1").arg(a.path);
    case AgentAction::ReplaceInFile: return QStringLiteral("replace_in_file %1").arg(a.path);
    case AgentAction::RunCommand:    return QStringLiteral("run_command `%1`").arg(a.command);
    case AgentAction::Done:          return QStringLiteral("done");
    case AgentAction::Invalid:       return QStringLiteral("%1 (invalid)").arg(a.name.isEmpty() ? QStringLiteral("?") : a.name);
    }
    return QString();
}

QString formatResults(const QList<ActionResult> &results, int step, int charLimit)
{
    // Share the budget between the actions so one huge file cannot crowd out
    // the compiler error that came after it.
    const int overhead = 400 + 160 * int(results.size());
    const int perItem = results.isEmpty() ? charLimit
                       : qMax(400, (charLimit - overhead) / int(results.size()));

    QString msg = QStringLiteral("Here are the results of step %1:\n").arg(step);
    int i = 1;
    for (const ActionResult &r : results) {
        msg += QStringLiteral("\n%1. %2 -> %3\n").arg(i++).arg(actionLabel(r.action), r.status);
        if (!r.output.isEmpty())
            msg += fence(truncateMiddle(r.output, perItem)) + QLatin1Char('\n');
    }
    msg += QStringLiteral(
        "\nThanks! What's the next step? If everything is finished, please send the done step with a short summary.");
    return msg;
}

} // namespace AgentProtocol
