#include "agentcontroller.h"

#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using Decision = AgentController::Decision;
using State = AgentController::State;

class TestAgentController : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QStringList m_asked;   // labels of actions the approver saw

    AgentController *make(Decision answer, bool autoLoop = true)
    {
        auto *c = new AgentController(this);
        c->workspace().setRoot(m_dir.path());
        AgentController::Options o;
        o.autoLoop = autoLoop;
        o.maxSteps = 5;
        o.commandTimeoutSec = 10;
        o.autoSendDelaySec = 0;
        c->setOptions(o);
        m_asked.clear();
        c->setApprover([this, answer](AgentAction &a, QString *note) {
            m_asked << a.path + a.command;
            if (answer == Decision::Reject)
                *note = "not like that";
            return answer;
        });
        return c;
    }

    static bool waitFor(AgentController *c, State s, int ms = 10000)
    {
        return QTest::qWaitFor([&] { return c->state() == s; }, ms);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);   // backups go to a test location
    }

    void init()
    {
        QDir(m_dir.path()).removeRecursively();
        QDir().mkpath(m_dir.path());
    }

    void startTaskSendsInstructions()
    {
        AgentController *c = make(Decision::Approve);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("add a readme");
        QCOMPARE(sent.size(), 1);
        QVERIFY(sent[0][0].toString().contains("add a readme"));
        QCOMPARE(c->state(), State::WaitingForCopilot);
    }

    void readsRunWithoutAskingAndLoopBack()
    {
        QFile f(m_dir.path() + "/hello.txt");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hi there");
        f.close();

        AgentController *c = make(Decision::Reject);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({R"({"action":"read_file","path":"hello.txt"})", R"({"action":"list_dir"})"});
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        QVERIFY(m_asked.isEmpty());
        QCOMPARE(sent.size(), 2);
        const QString results = sent[1][0].toString();
        QVERIFY2(results.contains("hi there"), qPrintable(results));
        QVERIFY(results.contains("hello.txt  (8 bytes)"));
        QCOMPARE(c->step(), 1);
    }

    void writeNeedsApproval()
    {
        AgentController *c = make(Decision::Approve);
        c->startTask("t");
        c->onReply({R"({"action":"write_file","path":"src/a.txt","content":"new"})"});
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        QCOMPARE(m_asked, QStringList{"src/a.txt"});
        QVERIFY(QFile::exists(m_dir.path() + "/src/a.txt"));
    }

    void rejectionSkipsTheRestAndTellsCopilot()
    {
        AgentController *c = make(Decision::Reject);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({R"({"action":"write_file","path":"a.txt","content":"x"})",
                    R"({"action":"run_command","command":"echo hi"})"});
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        QCOMPARE(m_asked.size(), 1);   // the command was never offered
        QVERIFY(!QFile::exists(m_dir.path() + "/a.txt"));
        const QString results = sent.last()[0].toString();
        QVERIFY(results.contains("rejected by the user: not like that"));
        QVERIFY(results.contains("skipped"));
    }

    void stopEndsEverything()
    {
        AgentController *c = make(Decision::Stop);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({R"({"action":"run_command","command":"echo hi"})"});
        QVERIFY(waitFor(c, State::Idle));
        QTest::qWait(50);
        QCOMPARE(sent.size(), 1);
        QVERIFY(!c->taskActive());
    }

    void commandOutputComesBack()
    {
        AgentController *c = make(Decision::Approve);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
#ifdef Q_OS_WIN
        c->onReply({R"({"action":"run_command","command":"echo agent-ok && exit /b 3"})"});
#else
        c->onReply({R"({"action":"run_command","command":"echo agent-ok; exit 3"})"});
#endif
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        const QString results = sent.last()[0].toString();
        QVERIFY2(results.contains("agent-ok"), qPrintable(results));
        QVERIFY(results.contains("exit code 3"));
    }

    void commandTimeoutIsReported()
    {
#ifndef Q_OS_WIN
        AgentController *c = make(Decision::Approve);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({R"({"action":"run_command","command":"sleep 30","timeout":1})"});
        QVERIFY(waitFor(c, State::WaitingForCopilot, 8000));
        QVERIFY(sent.last()[0].toString().contains("timeout"));
#endif
    }

    void autoSendWaitsBeforeReplying()
    {
        AgentController *c = make(Decision::Approve);
        AgentController::Options o = c->options();
        o.autoSendDelaySec = 1;
        c->setOptions(o);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        QElapsedTimer t;
        t.start();
        c->onReply({R"({"action":"list_dir"})"});
        QVERIFY(waitFor(c, State::ResultsReady));
        QCOMPARE(sent.size(), 1);                    // held back for now
        QVERIFY(waitFor(c, State::WaitingForCopilot, 5000));
        QVERIFY(t.elapsed() >= 900);
        QCOMPARE(sent.size(), 2);
    }

    void stopCancelsTheDelayedSend()
    {
        AgentController *c = make(Decision::Approve);
        AgentController::Options o = c->options();
        o.autoSendDelaySec = 1;
        c->setOptions(o);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({R"({"action":"list_dir"})"});
        QVERIFY(waitFor(c, State::ResultsReady));
        c->stop();
        QTest::qWait(1500);
        QCOMPARE(sent.size(), 1);
    }

    void repeatedStepPausesTheLoop()
    {
        AgentController *c = make(Decision::Approve);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({R"({"action":"list_dir"})"});
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        c->onReply({R"({"action":"list_dir"})"});
        QVERIFY(waitFor(c, State::ResultsReady));
        QCOMPARE(sent.size(), 2);                     // the repeat was not auto-sent
        c->sendPendingResults();
        QVERIFY(sent.last()[0].toString().contains("exactly the same step"));
    }

    void failingStreakPausesTheLoop()
    {
        AgentController *c = make(Decision::Approve);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        for (int i = 0; i < 3; ++i) {
            c->onReply({QString(R"({"action":"read_file","path":"missing%1"})").arg(i)});
            QVERIFY(QTest::qWaitFor([&] { return c->state() != State::RunningActions; }, 5000));
        }
        QCOMPARE(c->state(), State::ResultsReady);
        QCOMPARE(sent.size(), 3);                     // task + 2 auto results, third held
    }

    void undoRestoresAndTellsCopilot()
    {
        QFile f(m_dir.path() + "/keep.txt");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("original\n");
        f.close();

        AgentController *c = make(Decision::Approve, /*autoLoop*/ false);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({R"({"action":"write_file","path":"keep.txt","content":"// ... rest unchanged
"})",
                    R"({"action":"write_file","path":"new/added.txt","content":"x"})"});
        QVERIFY(waitFor(c, State::ResultsReady));
        QVERIFY(c->canUndo());
        QVERIFY(c->undoLabel().contains("added.txt"));

        QString msg;
        QVERIFY(c->undoLast(&msg));                  // removes the new file
        QVERIFY(!QFile::exists(m_dir.path() + "/new/added.txt"));
        QVERIFY(c->undoLast(&msg));                  // restores the original
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("original\n"));
        QVERIFY(!c->canUndo());
        QVERIFY(QFile::exists(c->backupDir() + "/1/keep.txt"));

        c->sendPendingResults();
        const QString next = sent.last()[0].toString();
        QVERIFY(next.contains("I removed the file new/added.txt"));
        QVERIFY(next.contains("I undid your last change to keep.txt"));
    }

    void searchRunsWithoutApproval()
    {
        QFile f(m_dir.path() + "/code.cpp");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("void needle() {}\n");
        f.close();
        AgentController *c = make(Decision::Reject);   // would reject anything it was asked about
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({"{\"action\":\"search\",\"query\":\"needle\"}", "{\"action\":\"find_files\",\"pattern\":\"*.cpp\"}"});
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        QVERIFY(m_asked.isEmpty());
        const QString results = sent.last()[0].toString();
        QVERIFY2(results.contains("code.cpp:1: void needle() {}"), qPrintable(results));
        QVERIFY(results.contains("code.cpp  (17 bytes)"));
    }

    void stopDuringSearchDropsTheResult()
    {
        AgentController *c = make(Decision::Approve);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({"{\"action\":\"search\",\"query\":\"x\"}"});
        c->stop();
        QTest::qWait(300);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(c->state(), State::Idle);
    }

    void notesAreReadAndWritten()
    {
        QFile n(m_dir.path() + "/AGENT_NOTES.md");
        QVERIFY(n.open(QIODevice::WriteOnly));
        n.write("# Old notes\nBuild with make.\n");
        n.close();
        AgentController *c = make(Decision::Reject, /*autoLoop*/ false);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        QVERIFY(sent[0][0].toString().contains("Build with make."));
        c->onReply({"{\"action\":\"update_notes\",\"content\":\"# New notes\"}"});
        QVERIFY(waitFor(c, State::ResultsReady));
        QVERIFY(m_asked.isEmpty());                    // no approval for notes
        QVERIFY(n.open(QIODevice::ReadOnly));
        QCOMPARE(n.readAll(), QByteArray("# New notes\n"));
        n.close();
        QString msg;
        QVERIFY(c->canUndo());
        QVERIFY(c->undoLast(&msg));                    // still undoable
        QVERIFY(n.open(QIODevice::ReadOnly));
        QVERIFY(n.readAll().startsWith("# Old notes"));
    }

    void pickedFilesGoInThePrompt()
    {
        QDir().mkpath(m_dir.path() + "/src");
        QFile a(m_dir.path() + "/src/a.cpp");
        QVERIFY(a.open(QIODevice::WriteOnly));
        a.write("int alpha() { return 1; }\n");
        a.close();
        QFile big(m_dir.path() + "/big.txt");
        QVERIFY(big.open(QIODevice::WriteOnly));
        for (int i = 0; i < 2000; ++i)
            big.write("filler line\n");
        big.close();

        AgentController *c = make(Decision::Approve);
        c->setFocusFiles({"src/a.cpp", "big.txt"}, true);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("rename alpha");
        const QString p = sent[0][0].toString();
        QVERIFY(p.contains("- src/a.cpp (2 lines)"));
        QVERIFY(p.contains("int alpha() { return 1; }"));                  // content included
        QVERIFY(p.contains("big.txt (2001 lines), too long to include"));  // named, not pasted
        QVERIFY(!p.contains("filler line"));
        QVERIFY(p.contains("only they should be changed"));
        QVERIFY(!p.contains("update the notes"));                           // notes off when restricted
    }

    void changesOutsidePickedFilesAreRefused()
    {
        AgentController *c = make(Decision::Approve);
        c->setFocusFiles({"src/a.cpp"}, true);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({"{\"action\":\"write_file\",\"path\":\"other.cpp\",\"content\":\"x\"}",
                    "{\"action\":\"write_file\",\"path\":\"./src/../src/a.cpp\",\"content\":\"y\"}"});
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        QVERIFY(m_asked.isEmpty());                               // refused without asking
        QVERIFY(!QFile::exists(m_dir.path() + "/other.cpp"));
        QVERIFY(!QFile::exists(m_dir.path() + "/src/a.cpp"));     // the rest of the step was skipped
        QVERIFY(sent.last()[0].toString().contains("refused: only these files may be changed in this task: src/a.cpp"));

        // The picked file itself, written as an odd but equivalent path: allowed, with approval.
        c->onReply({"{\"action\":\"write_file\",\"path\":\"./src/../src/a.cpp\",\"content\":\"y\"}",
                    "{\"action\":\"update_notes\",\"content\":\"n\"}"});
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        QCOMPARE(m_asked.size(), 1);
        QVERIFY(QFile::exists(m_dir.path() + "/src/a.cpp"));
        QVERIFY(!QFile::exists(m_dir.path() + "/AGENT_NOTES.md"));  // notes refused too
    }

    void pickedFilesWithoutRestriction()
    {
        AgentController *c = make(Decision::Approve);
        c->setFocusFiles({"src/a.cpp"}, false);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        QVERIFY(sent[0][0].toString().contains("as the place to start"));
        c->onReply({"{\"action\":\"write_file\",\"path\":\"other.cpp\",\"content\":\"x\"}"});
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        QVERIFY(QFile::exists(m_dir.path() + "/other.cpp"));
    }

    void excelEditNeedsApprovalAndUndoes()
    {
        QFile src(QStringLiteral(FIXTURES_DIR "/budget.xlsx"));
        QVERIFY(src.open(QIODevice::ReadOnly));
        const QByteArray original = src.readAll();
        QFile f(m_dir.path() + "/budget.xlsx");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(original);
        f.close();

        AgentController *c = make(Decision::Approve, /*autoLoop*/ false);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({"{\"action\":\"set_cells\",\"path\":\"budget.xlsx\",\"sheet\":\"Budget\",\"cells\":{\"B2\":150}}"});
        QVERIFY(waitFor(c, State::ResultsReady));
        QCOMPARE(m_asked, QStringList{"budget.xlsx"});   // asked first
        QVERIFY2(c->pendingResults().contains("1 cells set"), qPrintable(c->pendingResults()));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll() != original);
        f.close();
        QString msg;
        QVERIFY(c->undoLast(&msg));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), original);                 // byte for byte
    }

    void manualModeHoldsResults()
    {
        AgentController *c = make(Decision::Approve, /*autoLoop*/ false);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({R"({"action":"list_dir"})"});
        QVERIFY(waitFor(c, State::ResultsReady));
        QCOMPARE(sent.size(), 1);
        c->sendPendingResults();
        QCOMPARE(sent.size(), 2);
        QCOMPARE(c->state(), State::WaitingForCopilot);
    }

    void doneFinishes()
    {
        AgentController *c = make(Decision::Approve);
        QSignalSpy finished(c, &AgentController::taskFinished);
        c->startTask("t");
        c->onReply({R"({"action":"done","summary":"shipped"})"});
        QVERIFY(QTest::qWaitFor([&] { return finished.size() == 1; }, 5000));
        QCOMPARE(finished[0][0].toString(), QStringLiteral("shipped"));
        QCOMPARE(c->state(), State::Idle);
        QVERIFY(!c->taskActive());
    }

    void doneAfterAFailureIsNotDone()
    {
        AgentController *c = make(Decision::Approve);
        QSignalSpy finished(c, &AgentController::taskFinished);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        c->onReply({R"({"action":"read_file","path":"missing.txt"})", R"({"action":"done"})"});
        QVERIFY(waitFor(c, State::WaitingForCopilot));
        QCOMPARE(finished.size(), 0);
        QVERIFY(sent.last()[0].toString().contains("file not found"));
    }

    void plainReplyWaitsForUser()
    {
        AgentController *c = make(Decision::Approve);
        c->startTask("t");
        c->onReply({"int x = 1;"});
        QCOMPARE(c->state(), State::Idle);
        QVERIFY(c->taskActive());
    }

    void stepLimitStopsTheLoop()
    {
        for (int i = 0; i < 5; ++i) {
            QFile f(m_dir.path() + QString("/f%1.txt").arg(i));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray::number(i));
        }
        AgentController *c = make(Decision::Approve);
        QSignalSpy sent(c, &AgentController::sendPrompt);
        c->startTask("t");
        for (int i = 0; i < 5; ++i) {
            c->onReply({QString(R"({"action":"read_file","path":"f%1.txt"})").arg(i)});
            QVERIFY(QTest::qWaitFor([&] { return c->state() != State::RunningActions; }, 5000));
        }
        QCOMPARE(c->state(), State::ResultsReady);
        QCOMPARE(sent.size(), 5);   // task + 4 automatic result messages
    }
};

QTEST_GUILESS_MAIN(TestAgentController)
#include "tst_agentcontroller.moc"
