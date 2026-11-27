#pragma once

#include <QJsonObject>
#include <QString>

// One thing Copilot asked the app to do, parsed out of an ```agent block.
struct AgentAction
{
    enum Type {
        ReadFile,       // read-only, runs without asking
        ListDir,        // read-only, runs without asking
        Search,         // read-only: text search across the project
        FindFiles,      // read-only: files whose name or path matches a glob
        UpdateNotes,    // rewrites AGENT_NOTES.md; no approval, but undoable
        WriteFile,      // needs approval
        ReplaceInFile,  // needs approval
        RunCommand,     // needs approval
        Done,           // the task is finished
        Invalid         // could not be understood; reported back as an error
    };

    Type type = Invalid;
    QString name;          // the "action" string as Copilot wrote it
    QString path;
    QString content;       // write_file
    QString oldText;       // replace_in_file
    QString newText;       // replace_in_file
    QString command;       // run_command
    QString cwd;           // run_command, relative to the workspace
    QString summary;       // done
    QString error;         // why it is Invalid
    int timeoutSec = 0;    // run_command; 0 means "use the default"
    int startLine = 0;     // read_file, 1-based, 0 means "from the start"
    int endLine = 0;       // read_file, inclusive, 0 means "to the end"
    int depth = 1;         // list_dir
    QString query;         // search
    QString glob;          // search filter / find_files pattern
    bool regex = false;    // search
    bool caseSensitive = false;   // search
    int maxResults = 0;    // search / find_files; 0 means "use the default"
    QJsonObject raw;

    bool needsApproval() const
    {
        return type == WriteFile || type == ReplaceInFile || type == RunCommand;
    }
};

// What happened when an action ran, as it is reported back to Copilot.
struct ActionResult
{
    AgentAction action;
    bool ok = false;
    QString status;   // one line, e.g. "ok", "exit code 2", "rejected by user"
    QString output;   // file content, command output, error detail
};
