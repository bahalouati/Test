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

class QWebEnginePage;

// Talks to the Copilot chat page: types a prompt into its input box, presses
// send, then watches the page until the reply has stopped changing and hands
// over the code blocks that reply added.
class CopilotBridge : public QObject
{
    Q_OBJECT
public:
    explicit CopilotBridge(QWebEnginePage *page, QObject *parent = nullptr);

    // Extra CSS selectors tried before the built-in ones, as
    // {"input": [...], "send": [...], "stop": [...], "messages": [...], "codeBlocks": [...]}.
    void setSelectorOverrides(const QJsonObject &o) { m_overrides = o; }
    void setReplyTimeout(int seconds) { m_timeoutSec = seconds; }
    void setPollInterval(int ms) { m_poll.setInterval(ms); }

    bool isWaiting() const { return m_poll.isActive(); }

public slots:
    void sendPrompt(const QString &text);
    // Take whatever new code blocks are on the page now, e.g. after the user
    // pasted the prompt by hand.
    void readNow();
    void cancel();

signals:
    void sent();
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

    QPointer<QWebEnginePage> m_page;
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
};
