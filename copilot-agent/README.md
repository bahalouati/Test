# Copilot Agent

A Qt 6 desktop app that turns **Microsoft 365 Copilot Chat** into a coding agent.

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

You need Qt 6.2 or newer with the WebEngine module, and CMake 3.16 or newer.

```bash
# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-webengine-dev cmake g++

cmake -S copilot-agent -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/copilot-agent
```

On Windows and macOS, install Qt with the online installer and tick **Qt WebEngine**. Then
point CMake at it with `-DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2019_64`. On Windows, run
`windeployqt` on the built exe.

## Using it

1. Start the app and sign in to Microsoft 365 in the left pane. The profile is persistent,
   so you only sign in once.
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
