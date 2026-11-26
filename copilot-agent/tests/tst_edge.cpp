// Drives a real Edge/Chrome over the DevTools protocol, the way the app does
// in "Microsoft Edge" mode, against tests/mock_copilot.html: real typed input,
// real Enter, reply detection. Runs headless. Skips when no browser is found.
#include "copilotbridge.h"
#include "edgesession.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>
#include <utility>
#include <QtTest>

class TestEdge : public QObject
{
    Q_OBJECT

    QTemporaryDir m_profile;
    EdgeSession *m_edge = nullptr;
    QString m_browser;

    QVariant eval(const QString &script)
    {
        // Shared state: a late answer must not write into a dead stack frame.
        auto result = std::make_shared<std::pair<bool, QVariant>>(false, QVariant());
        m_edge->evaluate(script, [result](const QVariant &v) { *result = {true, v}; });
        if (!QTest::qWaitFor([&] { return result->first; }, 10000))
            qWarning("evaluate did not answer: %s", qPrintable(script.left(80)));
        return result->second;
    }

    bool openMock(const QString &mode)
    {
        QUrl url = QUrl::fromLocalFile(QStringLiteral(MOCK_PAGE_PATH));
        url.setQuery(QStringLiteral("mode=") + mode);
        auto done = std::make_shared<bool>(false);
        m_edge->call(QStringLiteral("Page.navigate"), QJsonObject{{QStringLiteral("url"), url.toString()}},
                     [done](const QJsonObject &, const QString &err) {
            if (!err.isEmpty())
                qWarning("Page.navigate: %s", qPrintable(err));
            *done = true;
        });
        if (!QTest::qWaitFor([&] { return *done; }, 10000))
            return false;
        return QTest::qWaitFor([&] {
            return eval(QStringLiteral("document.readyState === 'complete' && !!document.querySelector('.editor') "
                                       "&& location.search.indexOf('%1') >= 0").arg(mode)).toBool();
        }, 15000);
    }

private slots:
    void initTestCase()
    {
        m_browser = qEnvironmentVariable("COPILOTAGENT_TEST_BROWSER");
        if (m_browser.isEmpty())
            m_browser = EdgeSession::findBrowser();
        if (m_browser.isEmpty())
            QSKIP("No Edge or Chrome found (set COPILOTAGENT_TEST_BROWSER to run this test)");
        qInfo("Using browser: %s", qPrintable(m_browser));

        m_edge = new EdgeSession(this);
        m_edge->setBrowserPath(m_browser);
        m_edge->setProfileDir(m_profile.path());
        m_edge->setStartUrl(QStringLiteral("about:blank"));
        QStringList extra{QStringLiteral("--headless=new"), QStringLiteral("--disable-gpu")};
#ifdef Q_OS_LINUX
        extra << QStringLiteral("--no-sandbox");   // CI containers run as root
#endif
        m_edge->setExtraArguments(extra);

        // Which tab it picks, for CI logs.
        connect(m_edge, &EdgeSession::status, this, [](const QString &m) { qInfo("edge: %s", qPrintable(m)); });
        QSignalSpy ok(m_edge, &EdgeSession::connected);
        QSignalSpy bad(m_edge, &EdgeSession::failed);
        m_edge->start();
        QVERIFY2(QTest::qWaitFor([&] { return !ok.isEmpty() || !bad.isEmpty(); }, 60000), "browser never answered");
        QVERIFY2(bad.isEmpty(), bad.isEmpty() ? "" : qPrintable(bad[0][0].toString()));
        QVERIFY(m_edge->isConnected());
    }

    void reusesTheRunningBrowser()
    {
        // A second session on the same profile attaches instead of launching.
        EdgeSession second;
        second.setBrowserPath(m_browser);
        second.setProfileDir(m_profile.path());
        second.setStartUrl(QStringLiteral("about:blank"));
        QSignalSpy ok(&second, &EdgeSession::connected);
        second.start();
        QVERIFY(ok.wait(15000));
        QCOMPARE(second.launchedPid(), qint64(0));
    }

    void roundTrip_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("lexical") << "lexical";
        QTest::newRow("plain contenteditable") << "plain";
        QTest::newRow("textarea") << "textarea";
        QTest::newRow("strict (real Enter only)") << "strict";
    }

    void roundTrip()
    {
        QFETCH(QString, mode);
        QVERIFY(openMock(mode));

        CopilotBridge bridge;
        bridge.setScriptRunner([this](const QString &s, std::function<void(const QVariant &)> done) {
            m_edge->evaluate(s, done);
        });
        bridge.setTextInserter([this](const QString &t, std::function<void(bool)> done) {
            m_edge->insertText(t, done);
        });
        bridge.setKeyPresser([this] { m_edge->pressEnter(); return true; });
        bridge.setPollInterval(200);

        QSignalSpy sent(&bridge, &CopilotBridge::sent);
        QSignalSpy failed(&bridge, &CopilotBridge::sendFailed);
        QSignalSpy manual(&bridge, &CopilotBridge::manualSendNeeded);
        QSignalSpy reply(&bridge, &CopilotBridge::replyReady);

        const QString prompt = QStringLiteral("line one\nline \"two\" with \\ and ` and </script>\n"
                                              "and an inline {\"action\":\"done\"}");
        bridge.sendPrompt(prompt);
        QVERIFY2(QTest::qWaitFor([&] { return !sent.isEmpty() || !failed.isEmpty() || !manual.isEmpty(); }, 15000),
                 "no send result");
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed[0][0].toString()));
        QVERIFY2(manual.isEmpty(), "the prompt was typed but not sent");
        QVERIFY(reply.wait(20000));

        const QStringList blocks = reply[0][0].toStringList();
        QCOMPARE(blocks.size(), 2);
        QVERIFY2(blocks[0].contains("\"action\":\"list_dir\"") && blocks[0].contains("reply-1-len-"),
                 qPrintable(blocks[0]));
        QCOMPARE(eval(QStringLiteral("window.mockSent.length")).toInt(), 1);
        QCOMPARE(eval(QStringLiteral("window.mockSent[0]")).toString().trimmed(), prompt.trimmed());

        bridge.sendPrompt(QStringLiteral("again"));
        QVERIFY(reply.wait(20000));
        QVERIFY2(reply[1][0].toStringList().value(0).contains("reply-2-len-5"),
                 qPrintable(reply[1][0].toStringList().join(" | ")));
    }

    void unsentPromptIsNotAReply()
    {
        // A page that swallows Enter and has no send button: the prompt stays
        // in the box. That must end in manualSendNeeded, never a fake reply.
        QVERIFY(openMock(QStringLiteral("strict")));
        QVERIFY(eval(QStringLiteral(
            "document.querySelector('.editor').addEventListener('keydown', function (e) {"
            "  e.stopImmediatePropagation(); e.preventDefault(); }, true); true")).toBool());

        CopilotBridge bridge;
        bridge.setScriptRunner([this](const QString &s, std::function<void(const QVariant &)> done) {
            m_edge->evaluate(s, done);
        });
        bridge.setTextInserter([this](const QString &t, std::function<void(bool)> done) {
            m_edge->insertText(t, done);
        });
        bridge.setKeyPresser([this] { m_edge->pressEnter(); return true; });
        bridge.setPollInterval(200);
        QSignalSpy manual(&bridge, &CopilotBridge::manualSendNeeded);
        QSignalSpy reply(&bridge, &CopilotBridge::replyReady);

        bridge.sendPrompt(QStringLiteral("this will not be sent"));
        QVERIFY(manual.wait(20000));
        QTest::qWait(20000);   // longer than the plain-text reply rule needs
        QVERIFY(reply.isEmpty());

        // The user presses Send by hand: the reply is then picked up.
        QVERIFY(eval(QStringLiteral("document.getElementById('send').click(); true")).toBool());
        QVERIFY(reply.wait(20000));
        QCOMPARE(reply[0][0].toStringList().size(), 2);
    }

    void cleanupTestCase()
    {
        if (!m_edge || m_edge->launchedPid() <= 0)
            return;
        const QString pid = QString::number(m_edge->launchedPid());
#ifdef Q_OS_WIN
        QProcess::execute(QStringLiteral("taskkill"), {QStringLiteral("/PID"), pid, QStringLiteral("/T"), QStringLiteral("/F")});
#else
        QProcess::execute(QStringLiteral("kill"), {pid});
#endif
    }
};

QTEST_GUILESS_MAIN(TestEdge)
#include "tst_edge.moc"
