#pragma once

#include "agentaction.h"
#include "commandrunner.h"
#include "workspace.h"

#include <QList>
#include <QElapsedTimer>
#include <QObject>
#include <QQueue>

#include <functional>

// The agent loop, independent of any window or web page:
//   prompt -> Copilot reply -> parse actions -> (approve) -> run -> results -> prompt ...
// The page side only has to deliver reply code blocks to onReply() and send
// whatever sendPrompt() emits.
class AgentController : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, WaitingForCopilot, RunningActions, ResultsReady };
    Q_ENUM(State)

    enum class Decision { Approve, Reject, Stop };
    enum class LogKind { Info, Action, Result, Error, Done };

    // Called for every write and command, before it happens. The approver
    // may edit the action (e.g. the command line) and give a reason when it
    // rejects, which is passed on to Copilot.
    using Approver = std::function<Decision(AgentAction &action, QString *note)>;

    struct Options {
        bool autoLoop = true;
        int maxSteps = 25;
        int charLimit = 12000;       // per message sent to Copilot
        int commandTimeoutSec = 300;
        // Pause between Copilot's reply and the next automatic message, so
        // the loop paces itself like a person instead of firing back at once.
        int autoSendDelaySec = 5;
    };

    explicit AgentController(QObject *parent = nullptr);

    void setApprover(Approver a) { m_approver = std::move(a); }
    void setOptions(const Options &o) { m_opts = o; }
    Options options() const { return m_opts; }
    Workspace &workspace() { return m_ws; }

    State state() const { return m_state; }
    bool taskActive() const { return m_active; }
    int step() const { return m_step; }
    QString pendingResults() const { return m_pending; }
    QString lastPrompt() const { return m_lastPrompt; }

public slots:
    void startTask(const QString &task);
    void sendFollowUp(const QString &text);
    void sendPendingResults();
    // The code blocks that appeared in Copilot's newest reply.
    void onReply(const QStringList &codeBlocks);
    void onSendFailed(const QString &why);
    void stop();

signals:
    void sendPrompt(const QString &text);
    void log(AgentController::LogKind kind, const QString &text);
    void stateChanged(AgentController::State state);
    void taskFinished(const QString &summary);
    void commandOutput(const QString &chunk);

private:
    void setState(State s);
    void runNext();
    void finishStep();
    void record(const ActionResult &r);
    void send(QString text);   // by value: callers pass m_pending, which send() clears
    void skipRemaining(const QString &why);

    Workspace m_ws;
    CommandRunner m_runner;
    Approver m_approver;
    Options m_opts;

    State m_state = State::Idle;
    bool m_active = false;
    int m_step = 0;
    QQueue<AgentAction> m_queue;
    QList<ActionResult> m_results;
    AgentAction m_running;
    QString m_pending;
    QString m_lastPrompt;
    QString m_doneSummary;
    bool m_doneSeen = false;
    QElapsedTimer m_sinceReply;
    quint64 m_sendSerial = 0;   // invalidates a pending delayed send
};
