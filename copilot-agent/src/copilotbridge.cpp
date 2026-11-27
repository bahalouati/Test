#include "copilotbridge.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

static void initBridgeResource() { Q_INIT_RESOURCE(bridge); }

static QString normalizedText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text)
        if (!c.isSpace())
            out.append(c);
    return out;
}

CopilotBridge::CopilotBridge(QObject *parent)
    : QObject(parent)
{
    initBridgeResource();
    QFile f(QStringLiteral(":/bridge.js"));
    if (f.open(QIODevice::ReadOnly))
        m_js = QString::fromUtf8(f.readAll());
    m_poll.setInterval(900);
    connect(&m_poll, &QTimer::timeout, this, &CopilotBridge::poll);
}

void CopilotBridge::run(const QString &call, std::function<void(const QVariantMap &)> done)
{
    if (!m_runScript) {
        done({{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no browser connected")}});
        return;
    }
    const quint64 gen = m_generation;
    QPointer<CopilotBridge> self(this);
    m_runScript(m_js + QStringLiteral("\n;") + call, [self, gen, done](const QVariant &v) {
        if (!self || self->m_generation != gen)
            return;   // cancelled meanwhile
        done(v.toMap());
    });
}

static QString jsArg(const QJsonValue &v)
{
    // Wrap in an array so any string is valid JSON, then index it.
    return QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)) + QStringLiteral("[0]");
}

static QStringList toStrings(const QVariant &v)
{
    QStringList out;
    for (const QVariant &x : v.toList())
        out << x.toString();
    return out;
}

void CopilotBridge::takeBaseline(std::function<void()> then)
{
    run(QStringLiteral("__copilotAgent.snapshot(%1)").arg(jsArg(m_overrides)), [this, then](const QVariantMap &m) {
        m_baseline.clear();
        for (const QString &b : toStrings(m.value(QStringLiteral("blocks"))))
            m_baseline[b] += 1;
        m_baseTextLength = m.value(QStringLiteral("textLength")).toInt();
        m_haveBaseline = true;
        then();
    });
}

void CopilotBridge::sendPrompt(const QString &text)
{
    cancel();
    m_prompt = text;
    takeBaseline([this, text] { fillAndSubmit(text); });
}

void CopilotBridge::fillAndSubmit(const QString &text)
{
    const quint64 gen = m_generation;
    // Let the editor's framework process the input before sending.
    auto submitSoon = [this, gen] {
        QTimer::singleShot(700, this, [this, gen] {
            if (gen == m_generation)
                trySubmit(0);
        });
    };
    auto fillByScript = [this, text, submitSoon] {
        run(QStringLiteral("__copilotAgent.fill(%1, %2)").arg(jsArg(text), jsArg(m_overrides)),
            [this, submitSoon](const QVariantMap &m) {
            if (!m.value(QStringLiteral("ok")).toBool()) {
                emit sendFailed(m.value(QStringLiteral("error"), QStringLiteral("could not run the page script")).toString());
                return;
            }
            submitSoon();
        });
    };
    if (!m_insertText) {
        fillByScript();
        return;
    }
    // Real typed input where the transport offers it: editors treat it
    // exactly like the user typing. Fall back to the script if the text did
    // not arrive.
    run(QStringLiteral("__copilotAgent.prepareInput(%1)").arg(jsArg(m_overrides)),
        [this, gen, text, submitSoon, fillByScript](const QVariantMap &m) {
        if (!m.value(QStringLiteral("ok")).toBool()) {
            emit sendFailed(m.value(QStringLiteral("error"), QStringLiteral("could not run the page script")).toString());
            return;
        }
        m_insertText(text, [this, gen, submitSoon, fillByScript](bool ok) {
            if (gen != m_generation)
                return;
            if (!ok) {
                fillByScript();
                return;
            }
            run(QStringLiteral("__copilotAgent.snapshot(%1)").arg(jsArg(m_overrides)),
                [this, submitSoon, fillByScript](const QVariantMap &snap) {
                if (promptStillInInput(snap))
                    submitSoon();
                else
                    fillByScript();
            });
        });
    });
}

bool CopilotBridge::promptStillInInput(const QVariantMap &snapshot) const
{
    // Compare the start of what is in the box with the start of the prompt,
    // ignoring whitespace (editors re-flow it).
    const QString head = normalizedText(snapshot.value(QStringLiteral("inputHead")).toString());
    const QString want = normalizedText(m_prompt).left(30);
    return !head.isEmpty() && !want.isEmpty() && head.startsWith(want.left(qMin(want.size(), head.size())))
        && head.size() >= qMin(10, want.size());
}

// Send in escalating ways, checking after each whether the prompt really
// left the input box:
//   0: the page's send button, if one is found
//   1: a real Enter key press through the view
//   2: a synthetic Enter from script (works on simple pages only)
// then give up and ask the user to press Send, while still watching.
void CopilotBridge::trySubmit(int attempt)
{
    const quint64 gen = m_generation;
    auto checkLater = [this, gen, attempt] {
        QTimer::singleShot(1500, this, [this, gen, attempt] {
            if (gen != m_generation)
                return;
            run(QStringLiteral("__copilotAgent.snapshot(%1)").arg(jsArg(m_overrides)),
                [this, attempt](const QVariantMap &snap) {
                if (!promptStillInInput(snap)) {
                    startWatching();
                    emit sent();
                } else if (attempt < 2) {
                    trySubmit(attempt + 1);
                } else {
                    startWatching();
                    m_unsentWarned = true;
                    emit manualSendNeeded();
                }
            });
        });
    };

    switch (attempt) {
    case 0:
        run(QStringLiteral("__copilotAgent.submit(%1)").arg(jsArg(m_overrides)),
            [this, checkLater](const QVariantMap &r) {
            if (r.value(QStringLiteral("method")).toString() == QLatin1String("button"))
                checkLater();
            else
                trySubmit(1);   // no button found: go straight to Enter
        });
        break;
    case 1:
        if (!m_pressEnter) {
            trySubmit(2);
            break;
        }
        run(QStringLiteral("__copilotAgent.focusInput(%1)").arg(jsArg(m_overrides)),
            [this, checkLater](const QVariantMap &) {
            m_pressEnter();
            checkLater();
        });
        break;
    default:
        run(QStringLiteral("__copilotAgent.syntheticEnter(%1)").arg(jsArg(m_overrides)),
            [checkLater](const QVariantMap &) { checkLater(); });
        break;
    }
}

void CopilotBridge::startWatching()
{
    m_lastSignature.clear();
    m_stablePolls = 0;
    m_sawBusy = false;
    m_unsentWarned = false;
    m_clock.start();
    m_poll.start();
}

QStringList CopilotBridge::newBlocks(const QStringList &all) const
{
    // A block is new if it occurs more often now than before the prompt was
    // sent, unless it is text we sent ourselves (our message may render
    // code blocks too).
    QHash<QString, int> remaining = m_baseline;
    const QString prompt = normalizedText(m_prompt);
    QStringList out;
    for (const QString &b : all) {
        auto it = remaining.find(b);
        if (it != remaining.end() && it.value() > 0) {
            --it.value();
            continue;
        }
        const QString n = normalizedText(b);
        if (!n.isEmpty() && prompt.contains(n))
            continue;
        out << b;
    }
    return out;
}

void CopilotBridge::markSeen(const QStringList &blocks)
{
    // Handed over once; a later "Read reply" must not run them again.
    for (const QString &b : blocks)
        m_baseline[b] += 1;
}

void CopilotBridge::poll()
{
    const int elapsed = int(m_clock.elapsed() / 1000);
    if (elapsed > m_timeoutSec) {
        m_poll.stop();
        emit timedOut();
        return;
    }
    emit waiting(elapsed);

    run(QStringLiteral("__copilotAgent.snapshot(%1)").arg(jsArg(m_overrides)), [this, elapsed](const QVariantMap &m) {
        if (!m_poll.isActive() || !m.contains(QStringLiteral("blocks")))
            return;   // script did not run (page reloading, browser gone): not a reply
        const QStringList blocks = toStrings(m.value(QStringLiteral("blocks")));
        const int textLength = m.value(QStringLiteral("textLength")).toInt();
        const bool busy = m.value(QStringLiteral("busy")).toBool();
        m_sawBusy = m_sawBusy || busy;

        // Still sitting in the input box: nothing was sent, so nothing can
        // have been answered. Keep waiting (the user may press Send).
        if (promptStillInInput(m)) {
            m_stablePolls = 0;
            m_lastSignature.clear();
            return;
        }

        const QString sig = QString::number(textLength) + QLatin1Char('|') + QString::number(blocks.size())
            + QLatin1Char('|') + QString::fromLatin1(QCryptographicHash::hash(
                  (blocks.isEmpty() ? QString() : blocks.last()).toUtf8(), QCryptographicHash::Md5).toHex());
        m_stablePolls = (sig == m_lastSignature) ? m_stablePolls + 1 : 0;
        m_lastSignature = sig;

        if (busy || elapsed < 2)
            return;

        const QStringList fresh = newBlocks(blocks);
        const bool changed = textLength != m_baseTextLength;
        // With new code blocks, a short quiet period is enough. Without any,
        // the reply may be plain text, or Copilot may still be thinking
        // without showing a stop button, so wait longer before deciding.
        const bool finished = !fresh.isEmpty() ? m_stablePolls >= 3
                            : changed && (m_sawBusy ? m_stablePolls >= 3 : (m_stablePolls >= 10 && elapsed >= 15));
        if (!finished)
            return;
        m_poll.stop();
        markSeen(fresh);
        emit replyReady(fresh);
    });
}

void CopilotBridge::readNow()
{
    m_poll.stop();
    ++m_generation;
    auto take = [this] {
        run(QStringLiteral("__copilotAgent.snapshot(%1)").arg(jsArg(m_overrides)), [this](const QVariantMap &m) {
            const QStringList fresh = newBlocks(toStrings(m.value(QStringLiteral("blocks"))));
            markSeen(fresh);
            emit replyReady(fresh);
        });
    };
    if (m_haveBaseline) {
        take();
    } else {
        // Nothing sent through us yet: treat every block on the page as new.
        m_baseline.clear();
        m_haveBaseline = true;
        take();
    }
}

void CopilotBridge::describePage(std::function<void(const QString &)> done)
{
    run(QStringLiteral("__copilotAgent.describe(%1)").arg(jsArg(m_overrides)), [done](const QVariantMap &m) {
        done(m.value(QStringLiteral("ok")).toBool() ? m.value(QStringLiteral("text")).toString()
                                                   : QStringLiteral("(could not read the page: %1)")
                                                         .arg(m.value(QStringLiteral("error")).toString()));
    });
}

void CopilotBridge::cancel()
{
    m_poll.stop();
    ++m_generation;
}
