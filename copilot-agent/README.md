# Copilot Agent

A Qt desktop app (Qt 5.12+ or Qt 6.2+) that turns **Microsoft 365 Copilot Chat** into a coding agent.

The left side of the window is the real Copilot page in Qt WebEngine. You sign in with your
work account, so your organisation's enterprise data protection applies. The right side is the
agent panel. You pick a project folder and describe a task. The app then:

1. sends Copilot the task along with instructions for asking for actions,
2. waits for the reply and reads the ```` ```agent ```` JSON blocks in it,
3. runs the actions: reads and listings run straight away, but **every file write and every
   command shows a dialog first** (a diff for writes, an editable command line for commands),
4. pastes the results back into the chat, and repeats until Copilot sends `done`.

## Actions

| Action            | Asks you first | What it does                                          |
| ----------------- | -------------- | ----------------------------------------------------- |
| `list_dir`        | no             | Lists a folder (skips `.git`, `node_modules`, `build*`) |
| `read_file`       | no             | Reads a file, or a line range of it                   |
| `write_file`      | **yes**, with a diff | Creates or replaces a whole file                |
| `replace_in_file` | **yes**, with a diff | Replaces one exact, unique snippet               |
| `run_command`     | **yes**, editable    | Runs a shell command (`bash -c` or `cmd /C`) with a timeout |
| `done`            | no             | Ends the task                                         |

In the approval dialog, **Apply/Run** goes ahead and **Reject** skips the action and the rest
of that step. Any reason you type is sent to Copilot. **Stop agent** ends the task without
sending anything more.

Paths are resolved inside the project folder only. `..`, absolute paths and symlinks that point
outside the folder are refused before anything is touched.

## Build

You need Qt **5.12 or newer** or Qt **6.2 or newer**, with the WebEngine module, and CMake 3.16
or newer. When both majors are installed, CMake picks Qt 6; add `-DCOPILOTAGENT_QT_VERSION=5`
to use Qt 5.

```bash
# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-webengine-dev cmake g++

cmake -S copilot-agent -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/copilot-agent
```

For Qt 5.12.2, point CMake at the kit, for example
`-DCOPILOTAGENT_QT_VERSION=5 -DCMAKE_PREFIX_PATH=$HOME/Qt/5.12.2/gcc_64`. On Windows, Qt
WebEngine 5.12 needs an MSVC kit (msvc2017_64); MinGW kits do not include it.

### Windows, Qt 5.12.2, MSVC 2019, 32-bit

Qt 5.12 has no "msvc2019" kit. Use **MSVC2017 32-bit** (`win32_msvc2017`), which MSVC 2019 links
against fine because the two are binary-compatible. In the Qt installer, tick
*Qt 5.12.2 → MSVC 2017 32-bit* and *Qt WebEngine*. Then, from an
**x86 Native Tools Command Prompt for VS 2019**:

```bat
cmake -S copilot-agent -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCOPILOTAGENT_QT_VERSION=5 -DCMAKE_PREFIX_PATH=C:\Qt\5.12.2\msvc2017
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

## Two ways to reach Copilot

**In-app browser** (the default on Qt 5.15 and newer): the app types into the Copilot page
in its own window and reads the replies itself.

**Your own browser** (the default on Qt older than 5.15; switch it in **Settings**): Qt
5.12's built-in browser is Chromium 69, which cannot run today's Microsoft 365 pages. It fails
on JavaScript such as `?.` and `??`. In this mode the window shows only the agent panel:

1. Each message for Copilot is copied to the clipboard. Paste it into Copilot in Edge or
   Chrome and send it.
2. When Copilot has answered, copy the reply. Its copy button keeps the code blocks; a plain
   text selection works too. Then press **Paste reply**.
3. The actions run with the same approvals as in the in-app browser. The results are copied
   to the clipboard for you to paste back.

## Using it

1. Start the app and sign in to Microsoft 365 in the left pane, or in your own browser in
   external mode. The in-app profile is persistent, so you only sign in once.
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

The protocol, workspace sandbox, diff and agent-loop tests need no display. `tst_bridge` runs
the real `bridge.js` in headless Chromium against [`tests/mock_copilot.html`](tests/mock_copilot.html),
using three kinds of input box (Lexical-style editor, plain `contenteditable`, React
`textarea`). The mock stands in for the live Copilot page, which the tests do not reach.

## Layout

```
src/agentprotocol.*    instructions sent to Copilot, ```agent parser, results format
src/agentcontroller.*  the loop: reply -> actions -> approval -> run -> results
src/workspace.*        file actions, confined to the project folder
src/commandrunner.*    async shell commands with a timeout
src/copilotbridge.*    drives the page through bridge.js
src/bridge.js          injected into Copilot (isolated JS world)
src/approvaldialog.*   the diff / command confirmation
src/mainwindow.*       the window
```
