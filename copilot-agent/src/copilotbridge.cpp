#include "copilotbridge.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QWebEnginePage>
#include <QWebEngineScript>

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

CopilotBridge::CopilotBridge(QWebEnginePage *page, QObject *parent)
    : QObject(parent), m_page(page)
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
    if (!m_page) {
        done({{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no page")}});
        return;
    }
    const quint64 gen = m_generation;
    QPointer<CopilotBridge> self(this);
    // ApplicationWorld: same DOM, separate JS globals, unaffected by the
    // page's own scripts and content security policy.
    m_page->runJavaScript(m_js + QStringLiteral("\n;") + call, QWebEngineScript::ApplicationWorld,
                          [self, gen, done](const QVariant &v) {
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
    takeBaseline([this, text] {
        run(QStringLiteral("__copilotAgent.fill(%1, %2)").arg(jsArg(text), jsArg(m_overrides)),
            [this](const QVariantMap &m) {
            if (!m.value(QStringLiteral("ok")).toBool()) {
                emit sendFailed(m.value(QStringLiteral("error"), QStringLiteral("script error")).toString());
                return;
            }
            // Let the editor's framework process the input before sending.
            const quint64 gen = m_generation;
            QTimer::singleShot(700, this, [this, gen] {
                if (gen != m_generation)
                    return;
                run(QStringLiteral("__copilotAgent.submit(%1)").arg(jsArg(m_overrides)),
                    [this](const QVariantMap &r) {
                    if (!r.value(QStringLiteral("ok")).toBool()) {
                        emit sendFailed(r.value(QStringLiteral("error")).toString());
                        return;
                    }
                    m_lastSignature.clear();
                    m_stablePolls = 0;
                    m_sawBusy = false;
                    m_clock.start();
                    m_poll.start();
                    emit sent();
                });
            });
        });
    });
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
        if (!m_poll.isActive())
            return;
        const QStringList blocks = toStrings(m.value(QStringLiteral("blocks")));
        const int textLength = m.value(QStringLiteral("textLength")).toInt();
        const bool busy = m.value(QStringLiteral("busy")).toBool();
        m_sawBusy = m_sawBusy || busy;

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

void CopilotBridge::cancel()
{
    m_poll.stop();
    ++m_generation;
}
