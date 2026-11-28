# Copilot Agent

A Qt 5 desktop app (Qt 5.12 or newer, developed against 5.12.2) that turns **Microsoft 365 Copilot Chat** into a coding agent.

The left side of the window is the real Copilot page in Qt WebEngine. You sign in with your
work account, so your organisation's enterprise data protection applies. The right side is the
agent panel. You pick a project folder and describe a task. The app then:

1. sends Copilot the task along with instructions for asking for actions,
2. waits for the reply and reads the ```` ```agent ```` JSON blocks in it,
3. runs the actions: reads and listings run straight away, but **every file write and every
   command shows a dialog first** (a diff for writes, an editable command line for commands),
4. pastes the results back into the chat, and repeats until Copilot sends `done`.

## What it can and cannot do

- It **cannot** take screenshots, see your screen, or move the mouse or type into other programs.
- Without asking, it can only **read and list files inside the project folder** you choose.
- **Writing a file or running a command always needs your approval.** An approved command runs
  with your own user's permissions, which is why the dialog shows the exact command and lets
  you edit or reject it.
- It talks to Copilot only through the chat, like you would. The first message is a plain,
  polite request with no "system prompt" style instructions. It asks Copilot to keep
  commands simple and safe: build, test and inspect, but no deleting, no admin rights, and
  nothing interactive or long-running. With auto-loop on, it waits a few seconds after each
  reply before sending results (**Pause before auto-send** in Settings, default 5 s), so it
  keeps a human pace.

## When Copilot gets it wrong

Copilot can and will make mistakes, so the app assumes it will:

- **Approval dialog warnings.** The dialog shows a red box, and makes *Reject* the default
  button, when:
  - a write contains a placeholder such as `// ... rest of the file unchanged ...` that
    would delete real code;
  - a file would shrink to less than half its size, or be emptied;
  - a command deletes recursively, rewrites git history, needs admin rights, pipes a
    download into a shell, changes the registry or environment, or shuts the PC down.
- **Undo change** puts back the last file the agent wrote or edited, newest first, as many
  times as you like. A new file is removed again. Copilot is told in the next message, so it
  knows. Every overwritten file is also backed up under the app's data folder
  (`%LOCALAPPDATA%\CopilotAgent\Copilot Agent\backups` on Windows).
- **Stuck loops.** If Copilot repeats the exact same step with the same results, or three
  steps in a row all fail, auto-loop pauses for you to look at the chat. The next message
  asks Copilot to try a different approach.
- **Broken replies.** Invalid JSON, unknown actions, missing fields, edits whose "old" text
  doesn't match, and paths outside the project folder are refused. The error goes back to
  Copilot to correct. After a rejected or failed write or command, the rest of that step is
  skipped rather than run on a wrong assumption.
- The step limit (Settings) caps how long auto-loop runs unattended.

## Actions

| Action            | Asks you first | What it does                                          |
| ----------------- | -------------- | ----------------------------------------------------- |
| `search`          | no             | Text or regex search across the project; returns `file:line: text` |
| `find_files`      | no             | Files whose name (`*.cpp`) or path (`src/**/main*`) matches |
| `list_dir`        | no             | Lists a folder (skips ignored files and build folders) |
| `read_file`       | no             | Reads a file, or a line range of it; long files come in parts |
| `update_notes`    | no, but undoable | Rewrites `AGENT_NOTES.md`, the project notes        |
| `set_cells`       | **yes**, with a preview | Sets Excel cells (values, `=formulas`, `null` to empty) |
| `show_diff`       | no             | The diff under review, or of a file (git)             |
| `propose_plan`    | the plan, yes  | Plan first: the plan you approve before any change    |
| `report_findings` | no             | Review: the list of findings                          |
| `write_file`      | **yes**, with a diff | Creates or replaces a whole file                |
| `replace_in_file` | **yes**, with a diff | Replaces one exact, unique snippet               |
| `run_command`     | **yes**, editable    | Runs a shell command (`bash -c` or `cmd /C`) with a timeout |
| `done`            | no             | Ends the task                                         |

In the approval dialog, **Apply/Run** goes ahead and **Reject** skips the action and the rest
of that step. Any reason you type is sent to Copilot. **Stop agent** ends the task without
sending anything more.

Paths are resolved inside the project folder only. `..`, absolute paths and symlinks that point
outside the folder are refused before anything is touched.

## Modes: Task, Jira issue, Review

The **Mode** selector at the top of the Task box picks the kind of work. **Task** is the
default and needs no ticket: describe what to do, as always.

**Jira issue**: type a ticket key (`PROJ-123`, or paste its link) and press **Load**.
- **The ticket becomes the task:** summary, type, status, labels, components, fix versions,
  description, text custom fields found by name (acceptance criteria, steps to reproduce), the
  latest comments, and readable attachments (Word, Excel and text files up to 5 MB). Your own
  notes in the task box go along with it.
- **Branch:** **Start ticket** first offers to create and switch to a branch like
  `PROJ-123-short-title`, shown as a command you approve or edit. Reject it to stay on the
  current branch.
- **Wrap-up:** when Copilot finishes, the app drafts a comment for the ticket (summary, changed
  files, branch). You edit it, then **Post comment** or **Skip**. After posting, it offers to
  move the ticket to any status Jira allows from there (for example, In Review), or to keep
  the current one.
- **Connection:** **Jira settings...** holds the server URL, the sign-in type, the user and the
  token, plus an optional company CA certificate. Server and Data Center use a personal access
  token; Cloud uses e-mail and API token. These are the same settings as JiraDesk
  (`qt-jira-client`): if JiraDesk is set up on the computer, its connection is used.
  `JIRA_BASE_URL`, `JIRA_USER` (or `JIRA_EMAIL`), `JIRA_API_TOKEN` and `JIRA_AUTH_MODE` override
  them. The token is saved only if you tick "Remember the token".

**Plan first** (a checkbox, for Task and Jira issue): Copilot may only read and search until it
sends a plan. You **Approve plan**, **Ask for changes** with a note (Copilot sends an updated
plan), or **Stop agent**. After approval, every change still needs your approval as usual.

**Review**: a read-only review of the **uncommitted changes**, the **last commit**, or **this
branch compared with** another branch (`main` if left empty). The app runs git itself and gives
Copilot the change summary and, when it fits, the whole diff; otherwise Copilot asks for it
file by file. Copilot can only read, and reports its findings at the end. They open in their
own window, most severe first, with the details of each and **Copy as text**. Write what to
focus on in the task box.

## Word and Excel files

`.docx`/`.docm` and `.xlsx`/`.xlsm` files can be read, searched and edited without breaking them.

- **Reading and searching**: Word shows as paragraphs, with headings as `#` and tables as
  `| a | b |`. Excel shows one cell per line, sheet by sheet: `B2 = 120`, formulas with their last
  result (`B4 = =SUM(B2:B3)   [value: 1020.5]`), dates as dates.
- **Word edits** use `replace_in_file` with text from one paragraph. Only that text changes
  inside the document, so styles, images, tables and layout stay as they were. If the old text
  spans differently formatted parts, for example half bold, the new text takes the first
  part's formatting, and the approval dialog says so.
- **Excel edits** use `set_cells`. Values, `=formulas`, true/false and emptying cells all work,
  and each cell keeps its formatting. After an edit, cached formula results are dropped, so
  Excel (and LibreOffice) recalculate everything when the file is opened.
- The approval dialog previews the document text or cells before and after. **Undo change**
  restores the original file byte for byte.
- `write_file` is refused on Word, Excel and any other binary file, so they can't be overwritten
  with plain text. New Office files can't be created, and old `.doc`/`.xls` files aren't supported.

## Picking files for a task

To work on specific files, drag them from Explorer onto the **Files** box (or anywhere on the
window), use **Add files...**, or drop them onto the program's icon. Their content goes to
Copilot with the task, as much as fits in about 8,000 characters (smallest files first). Longer
ones are listed by name for Copilot to read in parts.

With **Only let Copilot change these files** ticked (the default), Copilot can still read and
search anything, but a write or edit to any other file is refused automatically, without a
dialog, and Copilot is told why. The project notes aren't updated then either. Commands still
go through the approval dialog as usual. Untick it to make the files a starting point only.
Files outside the project folder can't be added; if no project folder is chosen yet, the
first file's folder becomes the project folder.

## Big projects

- **Search first.** Copilot can search the code and find files by name without asking you,
  instead of opening files one by one. A search over about 8,000 files takes a fraction of a
  second. It stops at 100 matches by default (300 at most) and tells Copilot to narrow it with
  a folder or a file pattern.
- **Ignored files stay out.** Listings and searches skip whatever the project's `.gitignore`
  files ignore (the root one, nested ones and `.git/info/exclude`), plus build output,
  `node_modules`, virtualenvs and IDE folders. Binary files and files over 2 MB are not searched.
- **Long files come in parts.** Reading a file of more than 600 lines without a range returns
  its first 400 lines, with a note telling Copilot to ask for line ranges or to search. The
  instructions also ask Copilot to edit existing files with `replace_in_file` and to keep
  `write_file` for new or short files, since a long file rewritten in one reply can get cut off.
- **Project notes carry over.** `AGENT_NOTES.md` in the project folder holds what earlier
  conversations learned: layout, how to build and test, conventions, what changed. Each new
  task sends it to Copilot first, and Copilot updates it before finishing. Updating it needs no
  approval, since it touches nothing else, and **Undo change** reverts it like any other file.
  This makes it practical to start a fresh Copilot chat for each feature, which keeps Copilot
  from forgetting things in very long conversations. You can edit the file yourself, and commit
  it if it's useful to others.

## Build

You need **Qt 5.12 or newer** (Qt 5 only; Qt 6 is not supported) with the WebEngine module,
and CMake 3.16 or newer. No Python is needed: CI only uses it to download Qt.

```bash
# Ubuntu/Debian
sudo apt install qtbase5-dev qtwebengine5-dev cmake g++   # distro Qt 5.15

cmake -S copilot-agent -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/copilot-agent
```

For Qt 5.12.2, point CMake at the kit, for example
`-DCMAKE_PREFIX_PATH=$HOME/Qt/5.12.2/gcc_64`. On Windows, Qt
WebEngine 5.12 needs an MSVC kit (msvc2017_64); MinGW kits do not include it.

### Windows, Qt 5.12.2, MSVC 2019, 32-bit

Qt 5.12 has no "msvc2019" kit. Use **MSVC2017 32-bit** (`win32_msvc2017`), which MSVC 2019 links
against fine because the two are binary-compatible. In the Qt installer, tick
*Qt 5.12.2 → MSVC 2017 32-bit* and *Qt WebEngine*. Then, from an
**x86 Native Tools Command Prompt for VS 2019**:

```bat
cmake -S copilot-agent -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:\Qt\5.12.2\msvc2017
cmake --build build
ctest --test-dir build --output-on-failure
C:\Qt\5.12.2\msvc2017\bin\windeployqt.exe --release build\copilot-agent.exe
```

(Or `-G "Visual Studio 16 2019" -A Win32` instead of Ninja, then add `--config Release` to
the build and test commands.) `windeployqt` copies the Qt DLLs, `QtWebEngineProcess.exe` and
the WebEngine resources next to the exe so it runs on other machines. That machine also needs
the VC++ 2015-2019 x86 redistributable.

On other Windows setups and on macOS, install Qt with the online installer and tick **Qt WebEngine**. Then
point CMake at it with `-DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2019_64`. On Windows, run
`windeployqt` on the built exe.

## Which Copilot (Settings → "Copilot")

- **Microsoft 365 Copilot** (default): `m365.cloud.microsoft`, with your work account and your
  organisation's enterprise data protection.
- **Copilot, personal Microsoft account**: `copilot.com`. Choosing it shows a warning first: it is
  a consumer service, not covered by enterprise data protection, and everything the agent sends
  (task, file contents, command output) goes to Microsoft under the consumer terms. Keep company
  code and confidential data out of it unless your company allows it. While it is selected, a
  yellow banner stays at the top of the window.
- **Other address**: any other Copilot chat URL.

Work and personal Copilot each get their own Edge profile, so the two accounts never share a
window or sign-in. The app also never treats a work Copilot tab as the personal one, or the
other way round.

## Where Copilot runs (Settings → "Copilot runs in")

**Microsoft Edge, automatic** (default): the app opens Copilot in an Edge window of its own,
next to the app window. It types each message, presses Enter and reads the reply there, using
Edge's remote-debugging (DevTools) interface. Sign in once in that window: it has its own
profile, under the app's data folder, which keeps you signed in. Chrome is used if Edge isn't
installed, and **Settings → Browser program** can point at a specific one.
- If your organisation turns Edge remote debugging off (policy `RemoteDebuggingAllowed`), the
  app says so and hands that step over by clipboard instead, so nothing is lost.
- While that Edge window is open, its debugging port listens on 127.0.0.1 only, but other
  programs on your PC could drive that window too. Close it when you're done.

**My own browser, copy & paste**: works with any browser and any policy. Each message is
copied to the clipboard for you to paste into Copilot. Copy the reply (Copilot's copy button
keeps the code blocks) and press **Paste reply**.

**Built-in browser**: Qt's own WebEngine inside the app window. With Qt 5.12 this is Chromium
69 (2018), and Microsoft 365 Copilot does not render or work properly in it: the layout is
garbled and the Send button may be missing. It is kept for newer Qt 5 builds and testing.

## Using it

1. Start the app and press **Open Copilot in Edge**. Sign in to Microsoft 365 in the Edge
   window that opens; you only need to do this once.
2. **Choose...** a project folder.
3. Type a task and press **Start task**.
4. Approve or reject what it proposes. While a task runs, type in the task box and press
   **Send** to answer a question Copilot asked.

- **Auto-loop** sends the results back without asking, up to the step limit. Turn it off to
  review each step's results and press **Send results** yourself.
- **Read reply** parses the newest reply right away instead of waiting for it to settle.
- **Copy prompt** is the manual fallback: if the app cannot type into the page, paste the
  message yourself, send it, then press **Read reply**.

## When Microsoft changes the page

If Copilot clearly answered with an action but the log says "Copilot replied without
actions", press **Copy page details** in the toolbar. It copies a short outline of how the page
holds the input box, buttons and the reply, so whoever maintains the app can adjust it. The
outline includes the text of the last replies, so check it before sharing.

The app looks for replies in code blocks, in Microsoft 365's "code cards" (line numbers are
removed) and, failing those, for any `{"action": ...}` text in the reply.

The app finds the input box, the send and stop buttons, and the reply code blocks through CSS
selectors in [`src/bridge.js`](src/bridge.js). Each selector list falls back to generic ones
(any visible `textarea` or `contenteditable`, buttons labelled "Send" or "Stop", `<pre>`
blocks). The page's markup is not documented and changes. If the agent stops finding the
input box or never sees a reply, open **Settings** and add overrides, for example:

```json
{ "input": ["#m365-chat-editor-target-element"], "send": ["button[aria-label='Send']"] }
```

To find the right selectors, right-click an element in the page and use your browser's
devtools on the same page. To use Qt's built-in devtools, set
`QTWEBENGINE_REMOTE_DEBUGGING=9222` and open `http://localhost:9222`.

## Limits

- Copilot has a per-message length limit. Results are trimmed to **Max message size**
  (default 12 000 characters) by keeping the start and the end of each output.
- Commands cannot be interactive: stdin is closed, and they are killed at the timeout. On
  Windows, killing `cmd` does not always kill the processes it started.
- The app types into the page the way a user would. Check that your organisation's policy
  allows this kind of automation.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

The protocol, workspace sandbox, diff and agent-loop tests need no display. `tst_edge` drives a real Edge or Chrome headless over DevTools (skipped when neither is
installed; set `COPILOTAGENT_TEST_BROWSER` to choose one). `tst_bridge` runs
the real `bridge.js` in headless Chromium against [`tests/mock_copilot.html`](tests/mock_copilot.html),
using three kinds of input box (Lexical-style editor, plain `contenteditable`, React
`textarea`). The mock stands in for the live Copilot page, which the tests do not reach.

## Layout

```
src/agentprotocol.*    instructions sent to Copilot, ```agent parser, results format
src/agentcontroller.*  the loop: reply -> actions -> approval -> run -> results
src/workspace.*        file actions, confined to the project folder
src/commandrunner.*    async shell commands with a timeout
src/copilotbridge.*    drives the page through bridge.js (any transport)
src/edgesession.*      Edge/Chrome over the DevTools protocol
src/webenginetransport.*  Qt's built-in WebEngine
src/bridge.js          injected into Copilot (isolated JS world)
src/approvaldialog.*   the diff / command confirmation
src/mainwindow.*       the window
```
