#include "agentcontroller.h"

#include "agentprotocol.h"

#include <QDateTime>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QSysInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

using namespace AgentProtocol;

AgentController::AgentController(QObject *parent)
    : QObject(parent)
{
    connect(&m_runner, &CommandRunner::output, this, &AgentController::commandOutput);
    connect(&m_runner, &CommandRunner::finished, this,
            [this](int code, bool timedOut, bool failedToStart, const QString &out) {
        ActionResult r;
        r.action = m_running;
        r.output = out;
        if (failedToStart) {
            r.status = QStringLiteral("error: could not start the shell");
        } else if (timedOut) {
            r.status = QStringLiteral("killed after the %1 s timeout").arg(
                m_running.timeoutSec > 0 ? m_running.timeoutSec : m_opts.commandTimeoutSec);
        } else {
            r.ok = code == 0;
            r.status = QStringLiteral("exit code %1").arg(code);
        }
        if (r.output.trimmed().isEmpty() && !failedToStart)
            r.output.clear();
        record(r);
        if (!r.ok)
            skipRemaining(QStringLiteral("skipped: the command before it failed"));
        if (m_state == State::RunningActions)
            QTimer::singleShot(0, this, &AgentController::runNext);
    });
}

void AgentController::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void AgentController::send(QString text)
{
    if (!m_notes.isEmpty()) {
        text = m_notes.join(QLatin1Char('\n')) + QStringLiteral("\n\n") + text;
        m_notes.clear();
    }
    m_lastPrompt = text;
    m_pending.clear();
    ++m_sendSerial;
    setState(State::WaitingForCopilot);
    emit sendPrompt(text);
}

void AgentController::startTask(const QString &task)
{
    if (!m_ws.isValid()) {
        emit log(LogKind::Error, tr("Choose a project folder first."));
        return;
    }
    if (task.trimmed().isEmpty()) {
        emit log(LogKind::Error, tr("Describe the task first."));
        return;
    }
    stop();
    m_active = true;
    m_step = 0;
    m_lastSignature.clear();
    m_failStreak = 0;
    m_notes.clear();
    emit log(LogKind::Info, tr("New task in %1").arg(QDir::toNativeSeparators(m_ws.root())));
    const QString notes = m_ws.readNotes();
    if (!notes.isEmpty())
        emit log(LogKind::Info, tr("Including the project notes (%1).").arg(Workspace::notesFileName()));

    // The picked files: as much content as fits, small files first so as
    // many as possible come whole.
    QList<FocusFile> focus;
    for (const QString &path : m_focus) {
        AgentAction read;
        read.type = AgentAction::ReadFile;
        read.path = path;
        const ActionResult r = m_ws.readFile(read);
        FocusFile f;
        f.path = path;
        if (r.ok && !r.status.contains(QLatin1String("only lines"))) {
            f.content = r.output;
            f.lines = int(r.output.count(QLatin1Char('\n'))) + 1;
        } else {
            QFile file(QDir(m_ws.root()).filePath(path));
            if (file.open(QIODevice::ReadOnly))
                f.lines = int(file.readAll().count('\n')) + 1;
        }
        focus << f;
    }
    QList<int> order;
    for (int i = 0; i < focus.size(); ++i)
        order << i;
    std::sort(order.begin(), order.end(), [&](int x, int y) { return focus[x].content.size() < focus[y].content.size(); });
    int budget = kFocusContentBudget;
    for (int i : order) {
        if (!focus[i].content.isEmpty() && focus[i].content.size() <= budget) {
            focus[i].included = true;
            budget -= int(focus[i].content.size());
        }
    }
    if (!m_focus.isEmpty())
        emit log(LogKind::Info, onlyFocusFiles()
                 ? tr("Picked files: %1. Only these may be changed.").arg(m_focus.join(QStringLiteral(", ")))
                 : tr("Picked files: %1.").arg(m_focus.join(QStringLiteral(", "))));

    send(taskPrompt(task, QFileInfo(m_ws.root()).fileName(), QSysInfo::prettyProductName(),
                    CommandRunner::shellName(), notes, focusSection(focus, onlyFocusFiles()),
                    !onlyFocusFiles()));
}

void AgentController::sendFollowUp(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    if (m_state == State::RunningActions) {
        emit log(LogKind::Error, tr("Wait for the current actions to finish, or stop the agent."));
        return;
    }
    m_active = true;
    // Results the user never sent would be lost; send them along.
    send(m_pending.isEmpty() ? text : m_pending + QStringLiteral("\n\nAlso, from me: ") + text);
}

void AgentController::sendPendingResults()
{
    if (m_state != State::ResultsReady || m_pending.isEmpty())
        return;
    send(m_pending);
}

void AgentController::onSendFailed(const QString &why)
{
    emit log(LogKind::Error, tr("Could not send to Copilot: %1. Use \"Copy prompt\" and paste it yourself, then \"Read reply\".").arg(why));
    m_pending = m_lastPrompt;
    setState(State::ResultsReady);
}

void AgentController::onReply(const QStringList &codeBlocks)
{
    if (m_state == State::RunningActions)
        return;

    QList<AgentAction> actions = parseBlocks(codeBlocks);
    if (actions.isEmpty()) {
        // A plain-text reply: a question or an explanation. The user answers.
        emit log(LogKind::Info, tr("Copilot replied without actions. Answer it with \"Send\" or start a new task."));
        setState(State::Idle);
        return;
    }
    if (!m_active)
        m_active = true;

    m_sinceReply.start();
    ++m_stepGen;
    ++m_step;
    m_results.clear();
    m_queue.clear();
    m_doneSeen = false;
    m_doneSummary.clear();
    for (const AgentAction &a : actions)
        m_queue.enqueue(a);
    emit log(LogKind::Info, tr("Step %1: %n action(s)", nullptr, int(actions.size())).arg(m_step));
    setState(State::RunningActions);
    QTimer::singleShot(0, this, &AgentController::runNext);
}

void AgentController::record(const ActionResult &r)
{
    m_results.append(r);
    emit log(r.ok ? LogKind::Result : LogKind::Error,
             QStringLiteral("%1 -> %2").arg(actionLabel(r.action), r.status));
}

void AgentController::skipRemaining(const QString &why)
{
    while (!m_queue.isEmpty()) {
        AgentAction a = m_queue.dequeue();
        if (a.type == AgentAction::Done)
            continue;   // "done" after a failure is not done
        ActionResult r;
        r.action = a;
        r.status = why;
        record(r);
    }
}

void AgentController::runNext()
{
    if (m_state != State::RunningActions || m_runner.isRunning() || m_busy)
        return;
    if (m_queue.isEmpty()) {
        finishStep();
        return;
    }

    AgentAction a = m_queue.dequeue();
    emit log(LogKind::Action, actionLabel(a));

    if (a.type == AgentAction::Invalid) {
        ActionResult r;
        r.action = a;
        r.status = QStringLiteral("error: ") + a.error;
        record(r);
        QTimer::singleShot(0, this, &AgentController::runNext);
        return;
    }
    if (a.type == AgentAction::Done) {
        m_doneSeen = true;
        m_doneSummary = a.summary;
        skipRemaining(QStringLiteral("skipped: came after done"));
        QTimer::singleShot(0, this, &AgentController::runNext);
        return;
    }

    // Changes outside the files the user picked are refused outright.
    const bool changesAFile = a.type == AgentAction::WriteFile || a.type == AgentAction::ReplaceInFile
                           || a.type == AgentAction::SetCells || a.type == AgentAction::UpdateNotes;
    if (changesAFile && onlyFocusFiles()
        && (a.type == AgentAction::UpdateNotes || outsideFocus(a.path))) {
        ActionResult r;
        r.action = a;
        r.status = QStringLiteral("refused: only these files may be changed in this task: %1")
                       .arg(m_focus.join(QStringLiteral(", ")));
        record(r);
        if (a.type != AgentAction::UpdateNotes)
            skipRemaining(QStringLiteral("skipped: an earlier change in this step was refused"));
        QTimer::singleShot(0, this, &AgentController::runNext);
        return;
    }

    if (a.needsApproval()) {
        QString note;
        const Decision d = m_approver ? m_approver(a, &note) : Decision::Reject;
        if (d == Decision::Stop) {
            emit log(LogKind::Info, tr("Stopped by you. Nothing more is sent to Copilot."));
            stop();
            return;
        }
        if (d == Decision::Reject) {
            ActionResult r;
            r.action = a;
            r.status = note.trimmed().isEmpty()
                ? QStringLiteral("rejected by the user")
                : QStringLiteral("rejected by the user: ") + note.trimmed();
            record(r);
            skipRemaining(QStringLiteral("skipped: an earlier action in this step was rejected"));
            QTimer::singleShot(0, this, &AgentController::runNext);
            return;
        }
    }

    switch (a.type) {
    case AgentAction::ReadFile:      record(m_ws.readFile(a)); break;
    case AgentAction::ListDir:       record(m_ws.listDir(a)); break;
    case AgentAction::Search:
    case AgentAction::FindFiles: {
        // Can take seconds on a big project: run on a copy of the workspace
        // in the background, keep the window responsive.
        const Workspace ws = m_ws;
        m_busy = true;
        const quint64 gen = m_stepGen;
        auto *watcher = new QFutureWatcher<ActionResult>(this);
        connect(watcher, &QFutureWatcher<ActionResult>::finished, this, [this, watcher, gen] {
            watcher->deleteLater();
            m_busy = false;
            if (gen != m_stepGen) {
                // Stopped (or a new step began) meanwhile: drop the result,
                // but let a step that was waiting for us continue.
                if (m_state == State::RunningActions)
                    QTimer::singleShot(0, this, &AgentController::runNext);
                return;
            }
            if (m_state != State::RunningActions)
                return;
            record(watcher->result());
            QTimer::singleShot(0, this, &AgentController::runNext);
        });
        watcher->setFuture(QtConcurrent::run([ws, a] {
            return a.type == AgentAction::Search ? ws.search(a) : ws.findFiles(a);
        }));
        return;
    }
    case AgentAction::UpdateNotes: {
        // Only ever AGENT_NOTES.md, so no approval; still undoable.
        const FileChange before = snapshot(Workspace::notesFileName());
        const ActionResult r = m_ws.writeNotes(a);
        if (r.ok)
            remember(before);
        record(r);
        break;
    }
    case AgentAction::WriteFile: {
        const FileChange before = snapshot(a.path);
        const ActionResult r = m_ws.writeFile(a);
        if (r.ok)
            remember(before);
        record(r);
        if (!r.ok)
            skipRemaining(QStringLiteral("skipped: the write before it failed"));
        break;
    }
    case AgentAction::SetCells: {
        const FileChange before = snapshot(a.path);
        const ActionResult r = m_ws.setCells(a);
        if (r.ok)
            remember(before);
        record(r);
        if (!r.ok)
            skipRemaining(QStringLiteral("skipped: the edit before it failed"));
        break;
    }
    case AgentAction::ReplaceInFile: {
        const FileChange before = snapshot(a.path);
        const ActionResult r = m_ws.replaceInFile(a);
        if (r.ok)
            remember(before);
        record(r);
        if (!r.ok)
            skipRemaining(QStringLiteral("skipped: the edit before it failed"));
        break;
    }
    case AgentAction::RunCommand: {
        QString err;
        const QString dir = m_ws.resolve(a.cwd.isEmpty() ? QStringLiteral(".") : a.cwd, &err);
        if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
            ActionResult r;
            r.action = a;
            r.status = QStringLiteral("error: bad cwd: ") + (err.isEmpty() ? QStringLiteral("not a directory") : err);
            record(r);
            break;
        }
        m_running = a;
        const int timeout = a.timeoutSec > 0 ? qMin(a.timeoutSec, 3600) : m_opts.commandTimeoutSec;
        m_runner.start(a.command, dir, timeout);
        return;   // continues from CommandRunner::finished
    }
    default: break;
    }
    QTimer::singleShot(0, this, &AgentController::runNext);
}

void AgentController::finishStep()
{
    const bool anyFailed = std::any_of(m_results.cbegin(), m_results.cend(),
                                       [](const ActionResult &r) { return !r.ok; });
    if (m_doneSeen && !anyFailed) {
        emit log(LogKind::Done, m_doneSummary.isEmpty() ? tr("Task done.") : tr("Task done: %1").arg(m_doneSummary));
        m_active = false;
        m_pending.clear();
        setState(State::Idle);
        emit taskFinished(m_doneSummary);
        return;
    }

    if (m_results.isEmpty()) {
        setState(State::Idle);
        return;
    }

    // Copilot can get stuck: the same step with the same results again, or
    // step after step where nothing works. Tell it, and stop looping on our
    // own so a person looks at the chat.
    QString hold;
    const QByteArray sig = stepSignature(m_results);
    const bool repeated = !m_lastSignature.isEmpty() && sig == m_lastSignature;
    m_lastSignature = sig;
    const bool anyOk = std::any_of(m_results.cbegin(), m_results.cend(),
                                   [](const ActionResult &r) { return r.ok; });
    m_failStreak = anyOk ? 0 : m_failStreak + 1;
    if (repeated) {
        m_notes << QStringLiteral("(Note: that was exactly the same step as the previous one, with the same results. "
                                  "Could you try a different approach?)");
        hold = tr("Copilot repeated the previous step exactly. Auto-loop paused: check the chat, then press "
                  "\"Send results\" or answer it with \"Send\".");
    } else if (m_failStreak >= 3) {
        m_notes << QStringLiteral("(Note: the last %1 steps all failed. Could you step back and check your assumptions?)")
                       .arg(m_failStreak);
        hold = tr("Nothing has worked for %1 steps in a row. Auto-loop paused: check the chat, then press "
                  "\"Send results\" or answer it with \"Send\".").arg(m_failStreak);
    }

    m_pending = formatResults(m_results, m_step, m_opts.charLimit);
    if (!hold.isEmpty()) {
        emit log(LogKind::Error, hold);
        setState(State::ResultsReady);
        return;
    }
    if (m_opts.autoLoop && m_step < m_opts.maxSteps) {
        const qint64 wait = qint64(m_opts.autoSendDelaySec) * 1000
                          - (m_sinceReply.isValid() ? m_sinceReply.elapsed() : 0);
        if (wait <= 0) {
            send(m_pending);
            return;
        }
        // Wait in ResultsReady: "Send results" still sends at once, Stop or
        // a new task cancels.
        setState(State::ResultsReady);
        const quint64 serial = m_sendSerial;
        QTimer::singleShot(int(wait), this, [this, serial] {
            if (m_state == State::ResultsReady && m_active && serial == m_sendSerial && !m_pending.isEmpty())
                send(m_pending);
        });
        return;
    }
    if (m_opts.autoLoop)
        emit log(LogKind::Info, tr("Reached the limit of %1 steps. Press \"Send results\" to let it continue.").arg(m_opts.maxSteps));
    setState(State::ResultsReady);
}

void AgentController::setFocusFiles(const QStringList &paths, bool onlyThese)
{
    m_focus = paths;
    m_onlyFocus = onlyThese;
}

bool AgentController::outsideFocus(const QString &path) const
{
    // Compare where the paths actually point, so "./a.cpp", "src/../a.cpp"
    // and (on Windows) different letter case all count as the same file.
    QString err;
    const QString target = QDir::cleanPath(m_ws.resolve(path, &err));
    if (target.isEmpty())
        return true;
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    for (const QString &f : m_focus) {
        const QString allowed = QDir::cleanPath(m_ws.resolve(f, &err));
        if (!allowed.isEmpty() && allowed.compare(target, cs) == 0)
            return false;
    }
    return true;
}

AgentController::FileChange AgentController::snapshot(const QString &path) const
{
    FileChange c;
    c.path = path;
    QString err;
    const QString abs = m_ws.resolve(path, &err);
    QFile f(abs);
    if (!abs.isEmpty() && QFileInfo(abs).isFile() && f.open(QIODevice::ReadOnly)) {
        c.existed = true;
        c.before = f.readAll();
    }
    return c;
}

void AgentController::remember(const FileChange &c)
{
    // A copy on disk too, so an app crash or close does not lose it.
    if (m_backupDir.isEmpty())
        m_backupDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                    + QStringLiteral("/backups/")
                    + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    if (c.existed) {
        const QString dest = m_backupDir + QStringLiteral("/%1/").arg(m_undo.size() + 1) + c.path;
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFile f(dest);
        if (f.open(QIODevice::WriteOnly))
            f.write(c.before);
    }
    m_undo.append(c);
    emit undoChanged();
}

QString AgentController::undoLabel() const
{
    if (m_undo.isEmpty())
        return QString();
    const FileChange &c = m_undo.last();
    return c.existed ? tr("restore %1").arg(c.path) : tr("remove the new file %1").arg(c.path);
}

bool AgentController::undoLast(QString *message)
{
    if (!canUndo()) {
        *message = tr("Nothing to undo.");
        return false;
    }
    const FileChange c = m_undo.last();
    QString err;
    const QString abs = m_ws.resolve(c.path, &err);
    if (abs.isEmpty()) {
        *message = tr("Cannot undo %1: %2").arg(c.path, err);
        return false;
    }
    if (c.existed) {
        QSaveFile f(abs);
        if (!f.open(QIODevice::WriteOnly) || f.write(c.before) != c.before.size() || !f.commit()) {
            *message = tr("Cannot restore %1: %2").arg(c.path, f.errorString());
            return false;
        }
        m_notes << QStringLiteral("(Note: I undid your last change to %1, so it is back to how it was before that change.)")
                       .arg(c.path);
        *message = tr("Restored %1.").arg(c.path);
    } else {
        if (QFileInfo::exists(abs) && !QFile::remove(abs)) {
            *message = tr("Cannot remove %1.").arg(c.path);
            return false;
        }
        m_notes << QStringLiteral("(Note: I removed the file %1 that you created.)").arg(c.path);
        *message = tr("Removed %1.").arg(c.path);
    }
    m_undo.removeLast();
    // The results waiting to be sent may describe the undone change; the
    // note goes with them so Copilot sees both.
    emit undoChanged();
    return true;
}

void AgentController::stop()
{
    ++m_sendSerial;
    ++m_stepGen;
    m_queue.clear();
    m_runner.kill();
    m_active = false;
    setState(State::Idle);
}
