#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <functional>

// Talks to the Copilot chat page: types a prompt into its input box, presses
// send, then watches the page until the reply has stopped changing and hands
// over the code blocks that reply added.
//
// It does not care which browser shows the page. A transport supplies how to
// run JavaScript in it (and, optionally, how to type text and press Enter for
// real): Qt's built-in WebEngine (webenginetransport.h) or the user's own
// Edge/Chrome over the DevTools protocol (edgesession.h).
class CopilotBridge : public QObject
{
    Q_OBJECT
public:
    // Runs a script in the page and hands back the value of its last
    // expression (an invalid QVariant on any failure).
    using ScriptRunner = std::function<void(const QString &script, std::function<void(const QVariant &)> done)>;
    // Types text into the focused element as real input; done(false) on failure.
    using TextInserter = std::function<void(const QString &text, std::function<void(bool)> done)>;

    explicit CopilotBridge(QObject *parent = nullptr);

    void setScriptRunner(ScriptRunner r) { m_runScript = std::move(r); }
    void setTextInserter(TextInserter t) { m_insertText = std::move(t); }

    // Extra CSS selectors tried before the built-in ones, as
    // {"input": [...], "send": [...], "stop": [...], "messages": [...], "codeBlocks": [...]}.
    void setSelectorOverrides(const QJsonObject &o) { m_overrides = o; }
    void setReplyTimeout(int seconds) { m_timeoutSec = seconds; }
    void setPollInterval(int ms) { m_poll.setInterval(ms); }

    bool isWaiting() const { return m_poll.isActive(); }

    // Presses Enter for real (a key event through the view widget), which
    // pages accept where a script's synthetic key event is ignored. Set by
    // the window; returns false if it could not.
    void setKeyPresser(std::function<bool()> f) { m_pressEnter = std::move(f); }

public slots:
    void sendPrompt(const QString &text);
    // Take whatever new code blocks are on the page now, e.g. after the user
    // pasted the prompt by hand.
    void readNow();
    void cancel();

signals:
    void sent();
    // The prompt is typed in but could not be sent automatically. Watching
    // continues, so pressing Send in the page by hand is enough.
    void manualSendNeeded();
    void sendFailed(const QString &why);
    void waiting(int seconds);
    void replyReady(const QStringList &codeBlocks);
    void timedOut();

private:
    void run(const QString &call, std::function<void(const QVariantMap &)> done);
    void poll();
    QStringList newBlocks(const QStringList &all) const;
    void takeBaseline(std::function<void()> then);
    void markSeen(const QStringList &blocks);
    void trySubmit(int attempt);
    bool promptStillInInput(const QVariantMap &snapshot) const;
    void startWatching();

    ScriptRunner m_runScript;
    TextInserter m_insertText;
    void fillAndSubmit(const QString &text);
    QString m_js;
    QJsonObject m_overrides;
    QTimer m_poll;
    QElapsedTimer m_clock;
    int m_timeoutSec = 600;
    quint64 m_generation = 0;

    QString m_prompt;
    QHash<QString, int> m_baseline;     // block text -> how often it was already there
    bool m_haveBaseline = false;
    int m_baseTextLength = 0;
    QString m_lastSignature;
    int m_stablePolls = 0;
    bool m_sawBusy = false;
    bool m_unsentWarned = false;
    std::function<bool()> m_pressEnter;
};
