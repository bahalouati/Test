// Runs the real bridge.js against tests/mock_copilot.html in a headless
// Chromium: fill the input, press send, wait out the "streaming", and check
// that exactly the new agent block comes back.
#include "copilotbridge.h"
#include "webenginetransport.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QKeyEvent>
#include <QtTest>

class TestBridge : public QObject
{
    Q_OBJECT

    bool load(QWebEnginePage &page, const QString &mode)
    {
        QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
        page.load(QUrl(QStringLiteral("qrc:/mock_copilot.html?mode=") + mode));
        return loaded.wait(20000) && loaded[0][0].toBool();
    }

    static QVariant js(QWebEnginePage &page, const QString &script)
    {
        QVariant out;
        bool done = false;
        page.runJavaScript(script, [&](const QVariant &v) { out = v; done = true; });
        if (!QTest::qWaitFor([&] { return done; }, 5000))
            qWarning("runJavaScript did not answer");
        return out;
    }

private slots:
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
        QWebEngineProfile profile;   // off the record
        QWebEngineView view;         // a real widget, for real key presses
        auto *pagePtr = new QWebEnginePage(&profile, &view);
        QWebEnginePage &page = *pagePtr;
        view.setPage(pagePtr);
        view.resize(800, 600);
        view.show();
        QVERIFY(load(page, mode));

        CopilotBridge bridge;
        attachWebEnginePage(&bridge, &page);
        // Same as the app: Enter through the widget hosting the page.
        bridge.setKeyPresser([&view] {
            QWidget *target = view.focusProxy() ? view.focusProxy() : &view;
            target->setFocus();
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
            QCoreApplication::sendEvent(target, &press);
            QCoreApplication::sendEvent(target, &release);
            return true;
        });
        bridge.setPollInterval(200);
        QSignalSpy sent(&bridge, &CopilotBridge::sent);
        QSignalSpy failed(&bridge, &CopilotBridge::sendFailed);
        QSignalSpy reply(&bridge, &CopilotBridge::replyReady);

        // Multi-line, with characters that need escaping on the way in.
        const QString prompt = QStringLiteral("line one\nline \"two\" with \\ and ` and </script>\n"
                                              "and an inline {\"action\":\"done\"}");
        bridge.sendPrompt(prompt);
        QVERIFY2(QTest::qWaitFor([&] { return !sent.isEmpty() || !failed.isEmpty(); }, 10000), "no send result");
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed[0][0].toString()));
        QVERIFY(reply.wait(20000));

        const QStringList blocks = reply[0][0].toStringList();
        QCOMPARE(blocks.size(), 2);   // the agent block and the ordinary one; the parser drops the latter
        const QJsonObject o = QJsonDocument::fromJson(blocks[0].toUtf8()).object();
        QCOMPARE(o.value("action").toString(), QStringLiteral("list_dir"));
        QVERIFY2(o.value("path").toString().startsWith("reply-1-len-"), qPrintable(blocks[0]));

        // The page received the prompt intact (modulo trailing whitespace).
        const QString got = js(page, "window.mockSent[0]").toString();
        QCOMPARE(got.trimmed(), prompt.trimmed());

        // Second turn: only the second reply's blocks are new.
        bridge.sendPrompt(QStringLiteral("again"));
        QVERIFY(reply.wait(20000));
        const QStringList second = reply[1][0].toStringList();
        QCOMPARE(second.size(), 2);
        QVERIFY2(second[0].contains("reply-2-len-5"), qPrintable(second.join(" | ")));

        // Reading again finds nothing that was not already handed over.
        bridge.readNow();
        QVERIFY(reply.wait(5000));
        QVERIFY(reply[2][0].toStringList().isEmpty());
    }

    void reportsMissingInput()
    {
        QWebEngineProfile profile;
        QWebEnginePage page(&profile);
        QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
        page.setHtml(QStringLiteral("<html><body><p>Please sign in</p></body></html>"));
        QVERIFY(loaded.wait(10000));

        CopilotBridge bridge;
        attachWebEnginePage(&bridge, &page);
        QSignalSpy failed(&bridge, &CopilotBridge::sendFailed);
        bridge.sendPrompt(QStringLiteral("hello"));
        QVERIFY(failed.wait(10000));
        QVERIFY(failed[0][0].toString().contains("not found"));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    TestBridge t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_bridge.moc"
