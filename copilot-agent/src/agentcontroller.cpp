#include "agentcontroller.h"

#include "agentprotocol.h"

#include <QDir>
#include <QFileInfo>
#include <QSysInfo>
#include <QTimer>

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
    emit log(LogKind::Info, tr("New task in %1").arg(QDir::toNativeSeparators(m_ws.root())));
    send(taskPrompt(task, QFileInfo(m_ws.root()).fileName(), QSysInfo::prettyProductName(),
                    CommandRunner::shellName()));
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
    if (m_state != State::RunningActions || m_runner.isRunning())
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
    case AgentAction::WriteFile:     record(m_ws.writeFile(a)); break;
    case AgentAction::ReplaceInFile: {
        const ActionResult r = m_ws.replaceInFile(a);
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

    m_pending = formatResults(m_results, m_step, m_opts.charLimit);
    if (m_results.isEmpty()) {
        setState(State::Idle);
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

void AgentController::stop()
{
    ++m_sendSerial;
    m_queue.clear();
    m_runner.kill();
    m_active = false;
    setState(State::Idle);
}
