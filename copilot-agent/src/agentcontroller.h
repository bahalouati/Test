#pragma once

#include "agentaction.h"
#include "commandrunner.h"
#include "git.h"
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

    // Plan first and review restrict what Copilot may do:
    //   Planning - reading only, until the user approves a propose_plan;
    //   Review   - reading only, for the whole task.
    enum class Phase { Normal, Planning, Review };
    enum class PlanDecision { Approve, Revise, Stop };
    using PlanApprover = std::function<PlanDecision(const QString &plan, QString *note)>;
    void setPlanApprover(PlanApprover a) { m_planApprover = std::move(a); }
    void setPlanFirst(bool on) { m_planFirst = on; }
    Phase phase() const { return m_phase; }
    // What show_diff compares (also outside a review: the uncommitted work).
    void setDiffTarget(const Git::Target &t) { m_diffTarget = t; }
    // Files changed during this session, oldest first (from the undo list).
    QStringList changedFiles() const;

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

    // Files the user picked for the task (relative to the project folder).
    // Their content goes into the first message; with onlyThese, changes to
    // any other file (and to the notes) are refused without asking.
    void setFocusFiles(const QStringList &paths, bool onlyThese);
    QStringList focusFiles() const { return m_focus; }
    bool onlyFocusFiles() const { return m_onlyFocus && !m_focus.isEmpty(); }
    // Characters of picked files' content sent with the task, at most.
    static constexpr int kFocusContentBudget = 8000;
    void setOptions(const Options &o) { m_opts = o; }
    Options options() const { return m_opts; }
    Workspace &workspace() { return m_ws; }

    State state() const { return m_state; }
    bool taskActive() const { return m_active; }
    int step() const { return m_step; }
    QString pendingResults() const { return m_pending; }

    // Every file the agent wrote or edited, newest last, with what was there
    // before. Backups also go to disk (backupDir()) in case the app closes.
    bool canUndo() const { return !m_undo.isEmpty() && m_state != State::RunningActions; }
    QString undoLabel() const;
    // Puts the newest changed file back as it was. Copilot is told in the
    // next message, so its picture of the project stays right.
    bool undoLast(QString *message);
    QString backupDir() const { return m_backupDir; }
    QString lastPrompt() const { return m_lastPrompt; }

public slots:
    void startTask(const QString &task);
    // A read-only review; the prompt comes from AgentProtocol::reviewPrompt.
    void startReview(const QString &prompt, const Git::Target &target);
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
    void undoChanged();
    void findingsReported(const QJsonArray &findings);

private:
    void setState(State s);
    void runNext();
    void finishStep();
    void record(const ActionResult &r);
    void send(QString text);   // by value: callers pass m_pending, which send() clears
    void skipRemaining(const QString &why);
    bool outsideFocus(const QString &path) const;
    struct FileChange {
        QString path;          // relative to the workspace
        bool existed = false;
        QByteArray before;
    };
    FileChange snapshot(const QString &path) const;
    void remember(const FileChange &c);

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
    QList<FileChange> m_undo;
    QString m_backupDir;
    QStringList m_notes;        // told to Copilot with the next message
    QByteArray m_lastSignature; // of the previous step, to spot repeats
    int m_failStreak = 0;       // steps in a row where nothing succeeded
    bool m_busy = false;        // a background search is running
    QStringList m_focus;
    PlanApprover m_planApprover;
    bool m_planFirst = false;
    Phase m_phase = Phase::Normal;
    Git::Target m_diffTarget;
    QString refusalFor(const AgentAction &a) const;
    bool m_onlyFocus = true;
    quint64 m_stepGen = 0;      // changes on every new step and on stop
    quint64 m_sendSerial = 0;   // invalidates a pending delayed send
};
