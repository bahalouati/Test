#include "mainwindow.h"

#include "agentprotocol.h"
#include "approvaldialog.h"
#include "copilotbridge.h"
#include "linediff.h"
#include "edgesession.h"
#include "webenginetransport.h"
#include "webpage.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineView>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Copilot Agent"));
    m_settings = AppSettings::load();
    m_agent = new AgentController(this);
    m_agent->setApprover([this](AgentAction &a, QString *note) { return approve(a, note); });

    buildUi();
    wire();
    applySettings();

    const QString lastFolder = QSettings().value(QStringLiteral("workspace")).toString();
    if (!lastFolder.isEmpty() && QDir(lastFolder).exists())
        setFolder(lastFolder);
    if (isMode("builtin")) {
        m_view->load(QUrl(m_settings.url));
    } else if (isMode("edge")) {
        appendLog(AgentController::LogKind::Info,
                  tr("Copilot runs in Microsoft Edge. Press \"Open Copilot in Edge\" to sign in first, "
                     "or just start a task: Edge opens by itself."));
    } else {
        appendLog(AgentController::LogKind::Info,
                  tr("Copy & paste mode: open Copilot (%1) in your browser. Each message is copied "
                     "to the clipboard for you to paste; copy Copilot's reply back and press \"Paste reply\".")
                      .arg(m_settings.url));
    }
    updateButtons();
}

MainWindow::~MainWindow()
{
    saveGeometryForMode();
}

void MainWindow::saveGeometryForMode()
{
    if (!m_geometryMode.isEmpty())
        QSettings().setValue(QStringLiteral("geometry/") + m_geometryMode, saveGeometry());
}

void MainWindow::restoreGeometryForMode()
{
    if (m_geometryMode == m_settings.mode)
        return;
    saveGeometryForMode();
    m_geometryMode = m_settings.mode;
    const QByteArray g = QSettings().value(QStringLiteral("geometry/") + m_geometryMode).toByteArray();
    if (!g.isEmpty() && restoreGeometry(g))
        return;
    // First time in this mode: the panel alone is compact; with the built-in
    // page it shares a wide window.
    const QSize avail = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableSize()
                                                         : QSize(1280, 800);
    if (isMode("builtin"))
        resize(qMin(1500, avail.width() * 9 / 10), qMin(950, avail.height() * 9 / 10));
    else
        resize(qMin(720, avail.width() * 9 / 10), qMin(860, avail.height() * 9 / 10));
}

void MainWindow::buildUi()
{
    // Left: the real Copilot page. Right: the agent panel.
    QWebEngineProfile *profile = WebPage::createProfile(this);
    m_view = new QWebEngineView(this);
    m_view->setPage(new WebPage(profile, m_view));
    m_bridge = new CopilotBridge(this);
    m_edge = new EdgeSession(this);
    m_edge->setProfileDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/edge-profile"));
    // Set up for the built-in page; useTransportForMode() switches to Edge.
    attachWebEnginePage(m_bridge, m_view->page());
    // A real Enter through the widget that hosts the page: Chromium treats
    // it like the user's own key press, unlike a script-made key event.
    m_bridge->setKeyPresser([this] {
        if (isMode("edge")) {
            m_edge->pressEnter();
            return true;
        }
        QWidget *target = m_view->focusProxy() ? m_view->focusProxy() : m_view;
        m_view->setFocus();
        target->setFocus();
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
        QCoreApplication::sendEvent(target, &press);
        QCoreApplication::sendEvent(target, &release);
        return true;
    });

    auto *tb = m_browserBar = addToolBar(tr("Browser"));
    tb->setMovable(false);
    tb->addAction(m_view->pageAction(QWebEnginePage::Back));
    tb->addAction(m_view->pageAction(QWebEnginePage::Forward));
    tb->addAction(m_view->pageAction(QWebEnginePage::Reload));
    auto *home = tb->addAction(tr("Copilot"));
    connect(home, &QAction::triggered, this, [this] { m_view->load(QUrl(m_settings.url)); });
    m_address = new QLineEdit(this);
    m_address->setReadOnly(true);
    tb->addWidget(m_address);
    // Settings stay reachable when the browser toolbar is hidden.
    auto *appBar = addToolBar(tr("App"));
    appBar->setMovable(false);
    m_openEdge = appBar->addAction(tr("Open Copilot in Edge"));
    m_openEdge->setToolTip(tr("Opens Copilot in the app's Edge window, e.g. to sign in before starting"));
    connect(m_openEdge, &QAction::triggered, this, [this] {
        m_edge->setStartUrl(m_settings.url);
        m_edge->setBrowserPath(m_settings.browserPath);
        m_edge->start();
    });
    auto *settings = appBar->addAction(tr("Settings"));
    connect(settings, &QAction::triggered, this, [this] {
        SettingsDialog d(m_settings, this);
        if (d.exec() != QDialog::Accepted)
            return;
        const QString oldUrl = m_settings.url;
        m_settings = d.settings();
        m_settings.autoLoop = m_autoLoop->isChecked();
        m_settings.save();
        applySettings();
        if (isMode("builtin") && (oldUrl != m_settings.url || m_view->url().isEmpty()))
            m_view->load(QUrl(m_settings.url));
    });

    auto *panel = new QWidget(this);
    auto *pl = new QVBoxLayout(panel);

    auto *folderBox = new QGroupBox(tr("Project folder"), panel);
    auto *fl = new QHBoxLayout(folderBox);
    m_folder = new QLineEdit(folderBox);
    m_folder->setReadOnly(true);
    m_folder->setPlaceholderText(tr("The agent can only touch files in here"));
    auto *browse = new QPushButton(tr("Choose..."), folderBox);
    connect(browse, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    fl->addWidget(m_folder, 1);
    fl->addWidget(browse);
    pl->addWidget(folderBox);

    auto *taskBox = new QGroupBox(tr("Task"), panel);
    auto *tl = new QVBoxLayout(taskBox);
    m_task = new QPlainTextEdit(taskBox);
    m_task->setPlaceholderText(tr("What should Copilot build or change?\n"
                                  "e.g. \"Add a --verbose flag to the CLI and a test for it.\"\n\n"
                                  "While a task runs, type here and press Send to answer Copilot."));
    m_task->setMaximumHeight(140);
    tl->addWidget(m_task);
    auto *row = new QHBoxLayout;
    m_start = new QPushButton(tr("Start task"), taskBox);
    m_followUp = new QPushButton(tr("Send"), taskBox);
    m_stop = new QPushButton(tr("Stop"), taskBox);
    m_start->setToolTip(tr("Sends the agent instructions plus your task as a new request"));
    m_followUp->setToolTip(tr("Sends the text as a plain message in the current conversation"));
    row->addWidget(m_start);
    row->addWidget(m_followUp);
    row->addStretch();
    row->addWidget(m_stop);
    tl->addLayout(row);

    auto *loopRow = new QHBoxLayout;
    m_autoLoop = new QCheckBox(tr("Auto-loop: send results back automatically"), taskBox);
    m_maxSteps = new QSpinBox(taskBox);
    m_maxSteps->setRange(1, 500);
    m_maxSteps->setPrefix(tr("max "));
    m_maxSteps->setSuffix(tr(" steps"));
    loopRow->addWidget(m_autoLoop);
    loopRow->addStretch();
    loopRow->addWidget(m_maxSteps);
    tl->addLayout(loopRow);
    pl->addWidget(taskBox);

    auto *manual = new QHBoxLayout;
    m_sendResults = new QPushButton(tr("Send results"), panel);
    m_readReply = new QPushButton(tr("Read reply"), panel);
    m_copyPrompt = new QPushButton(tr("Copy prompt"), panel);
    m_sendResults->setToolTip(tr("Send the results of the last step to Copilot"));
    m_readReply->setToolTip(tr("Parse Copilot's newest reply now instead of waiting"));
    m_copyPrompt->setToolTip(tr("Copy the message to the clipboard to paste it into Copilot yourself"));
    m_pasteReply = new QPushButton(tr("Paste reply"), panel);
    m_pasteReply->setToolTip(tr("Copy Copilot's reply in your browser, then press this to run its actions"));
    manual->addWidget(m_sendResults);
    manual->addWidget(m_readReply);
    manual->addWidget(m_copyPrompt);
    manual->addWidget(m_pasteReply);
    m_undo = new QPushButton(tr("Undo change"), panel);
    m_undo->setToolTip(tr("Put back the last file the agent wrote or edited; Copilot is told"));
    manual->addWidget(m_undo);
    pl->addLayout(manual);

    m_log = new QTextBrowser(panel);
    m_log->setOpenLinks(false);
    pl->addWidget(m_log, 1);

    auto *split = new QSplitter(this);
    split->addWidget(m_view);
    split->addWidget(panel);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    m_split = split;
    panel->setMinimumWidth(360);
    setCentralWidget(split);

    m_status = new QLabel(this);
    statusBar()->addWidget(m_status, 1);
}

void MainWindow::wire()
{
    connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl &u) {
        m_address->setText(u.toString());
    });

    connect(m_start, &QPushButton::clicked, this, [this] {
        m_agent->startTask(m_task->toPlainText());
        if (m_agent->taskActive())
            m_task->clear();
    });
    connect(m_followUp, &QPushButton::clicked, this, [this] {
        const QString t = m_task->toPlainText();
        if (t.trimmed().isEmpty())
            return;
        m_agent->sendFollowUp(t);
        m_task->clear();
    });
    connect(m_stop, &QPushButton::clicked, this, [this] {
        m_bridge->cancel();
        m_agent->stop();
        appendLog(AgentController::LogKind::Info, tr("Stopped."));
    });
    connect(m_sendResults, &QPushButton::clicked, m_agent, &AgentController::sendPendingResults);
    connect(m_readReply, &QPushButton::clicked, m_bridge, &CopilotBridge::readNow);
    connect(m_copyPrompt, &QPushButton::clicked, this, [this] {
        const QString text = !m_agent->pendingResults().isEmpty() ? m_agent->pendingResults()
                                                                  : m_agent->lastPrompt();
        QApplication::clipboard()->setText(text);
        appendLog(AgentController::LogKind::Info,
                  tr("Copied %1 characters. Paste into Copilot, send, then press \"Read reply\" once it has answered.")
                      .arg(text.size()));
    });
    connect(m_autoLoop, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.autoLoop = on;
        m_settings.save();
        applySettings();
    });
    connect(m_maxSteps, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        m_settings.maxSteps = v;
        m_settings.save();
        applySettings();
    });

    connect(m_pasteReply, &QPushButton::clicked, this, &MainWindow::pasteReply);
    connect(m_undo, &QPushButton::clicked, this, [this] {
        const QString what = m_agent->undoLabel();
        if (QMessageBox::question(this, tr("Undo change"), tr("Undo the last change: %1?").arg(what))
            != QMessageBox::Yes)
            return;
        QString msg;
        const bool ok = m_agent->undoLast(&msg);
        appendLog(ok ? AgentController::LogKind::Info : AgentController::LogKind::Error,
                  ok ? msg + tr(" Copilot will be told with the next message.") : msg);
        updateButtons();
    });
    connect(m_agent, &AgentController::undoChanged, this, &MainWindow::updateButtons);
    connect(m_agent, &AgentController::sendPrompt, this, &MainWindow::deliverPrompt);
    connect(m_agent, &AgentController::log, this, &MainWindow::appendLog);
    connect(m_agent, &AgentController::stateChanged, this, &MainWindow::updateButtons);
    connect(m_agent, &AgentController::taskFinished, this, [this](const QString &summary) {
        QApplication::alert(this);
        statusBar()->showMessage(tr("Task done"), 10000);
        Q_UNUSED(summary);
    });

    connect(m_bridge, &CopilotBridge::sent, this, [this] {
        appendLog(AgentController::LogKind::Info, tr("Sent to Copilot, waiting for the reply..."));
    });
    connect(m_bridge, &CopilotBridge::sendFailed, m_agent, &AgentController::onSendFailed);

    connect(m_edge, &EdgeSession::status, this, [this](const QString &msg) { m_status->setText(msg); });
    connect(m_edge, &EdgeSession::connected, this, [this] {
        appendLog(AgentController::LogKind::Info,
                  tr("Connected to Copilot in Edge. If it asks you to sign in, do that in the Edge window."));
        if (m_edgeFellBack) {
            m_edgeFellBack = false;
            m_pasteReply->setVisible(false);
        }
        if (!m_queuedPrompt.isEmpty()) {
            const QString text = m_queuedPrompt;
            m_queuedPrompt.clear();
            // Give a freshly opened page a moment to finish loading.
            QTimer::singleShot(3000, this, [this, text] { m_bridge->sendPrompt(text); });
        }
    });
    connect(m_edge, &EdgeSession::failed, this, [this](const QString &why) {
        appendLog(AgentController::LogKind::Error, tr("Could not use Edge: %1.").arg(why));
        if (!m_queuedPrompt.isEmpty()) {
            // Do not lose the step: hand it over by clipboard this once.
            m_edgeFellBack = true;
            m_pasteReply->setVisible(true);
            const QString text = m_queuedPrompt;
            m_queuedPrompt.clear();
            deliverByClipboard(text);
        }
    });
    connect(m_edge, &EdgeSession::disconnected, this, [this] {
        m_bridge->cancel();
        appendLog(AgentController::LogKind::Info,
                  tr("The Edge window was closed. It opens again with the next message."));
        updateButtons();
    });
    connect(m_bridge, &CopilotBridge::manualSendNeeded, this, [this] {
        QApplication::alert(this);
        appendLog(AgentController::LogKind::Error,
                  tr("The message is typed into Copilot but could not be sent automatically. "
                     "Click into the Copilot box and press Enter (or its Send button); "
                     "the app keeps watching and continues once Copilot answers."));
        m_status->setText(tr("Press Enter in the Copilot box to send"));
    });
    connect(m_bridge, &CopilotBridge::replyReady, m_agent, &AgentController::onReply);
    connect(m_bridge, &CopilotBridge::waiting, this, [this](int s) {
        m_status->setText(tr("Waiting for Copilot... %1 s").arg(s));
    });
    connect(m_bridge, &CopilotBridge::timedOut, this, [this] {
        appendLog(AgentController::LogKind::Error,
                  tr("No finished reply within %1 s. Press \"Read reply\" when Copilot is done.").arg(m_settings.replyTimeoutSec));
        updateButtons();
    });
}

void MainWindow::applySettings()
{
    AgentController::Options o;
    o.autoLoop = m_settings.autoLoop;
    o.maxSteps = m_settings.maxSteps;
    o.charLimit = m_settings.charLimit;
    o.commandTimeoutSec = m_settings.commandTimeoutSec;
    o.autoSendDelaySec = m_settings.autoSendDelaySec;
    m_agent->setOptions(o);
    m_bridge->setSelectorOverrides(m_settings.selectors());
    // Only the built-in mode shows a page here; in the others the window is
    // just the agent panel and Copilot lives in a browser window of its own.
    const bool builtin = isMode("builtin");
    m_view->setVisible(builtin);
    m_browserBar->setVisible(builtin);
    m_openEdge->setVisible(isMode("edge"));
    m_readReply->setVisible(!isMode("clipboard"));
    m_pasteReply->setVisible(isMode("clipboard") || m_edgeFellBack);
    m_edge->setStartUrl(m_settings.url);
    m_edge->setBrowserPath(m_settings.browserPath);
    useTransportForMode();
    restoreGeometryForMode();
    if (builtin) {
        // Give the page most of the width once the window has its real size.
        QTimer::singleShot(0, this, [this] {
            const int w = m_split->width();
            m_split->setSizes({w - qMax(420, w * 2 / 5), qMax(420, w * 2 / 5)});
        });
    }
    m_bridge->setReplyTimeout(m_settings.replyTimeoutSec);

    const QSignalBlocker b1(m_autoLoop), b2(m_maxSteps);
    m_autoLoop->setChecked(m_settings.autoLoop);
    m_maxSteps->setValue(m_settings.maxSteps);
}

void MainWindow::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Project folder"), m_folder->text());
    if (!dir.isEmpty())
        setFolder(dir);
}

void MainWindow::setFolder(const QString &path)
{
    if (m_agent->state() == AgentController::State::RunningActions) {
        QMessageBox::information(this, windowTitle(), tr("Stop the agent before changing the folder."));
        return;
    }
    m_agent->workspace().setRoot(path);
    m_folder->setText(QDir::toNativeSeparators(m_agent->workspace().root()));
    QSettings().setValue(QStringLiteral("workspace"), m_agent->workspace().root());
    updateButtons();
}

void MainWindow::appendLog(AgentController::LogKind kind, const QString &text)
{
    QString color, tag;
    switch (kind) {
    case AgentController::LogKind::Info:   color = QStringLiteral("#57606a"); tag = QStringLiteral("info"); break;
    case AgentController::LogKind::Action: color = QStringLiteral("#0969da"); tag = QStringLiteral("action"); break;
    case AgentController::LogKind::Result: color = QStringLiteral("#1a7f37"); tag = QStringLiteral("ok"); break;
    case AgentController::LogKind::Error:  color = QStringLiteral("#cf222e"); tag = QStringLiteral("error"); break;
    case AgentController::LogKind::Done:   color = QStringLiteral("#8250df"); tag = QStringLiteral("done"); break;
    }
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_log->append(QStringLiteral("<div><span style='color:#8c959f'>%1</span> "
                                 "<b style='color:%2'>[%3]</b> <span style='white-space:pre-wrap'>%4</span></div>")
                      .arg(time, color, tag, text.toHtmlEscaped()));
}

void MainWindow::updateButtons()
{
    using S = AgentController::State;
    const S s = m_agent->state();
    const bool haveFolder = m_agent->workspace().isValid();
    m_start->setEnabled(haveFolder && s != S::RunningActions);
    m_followUp->setEnabled(haveFolder && s != S::RunningActions);
    m_stop->setEnabled(s != S::Idle || m_bridge->isWaiting());
    m_sendResults->setEnabled(s == S::ResultsReady && !m_agent->pendingResults().isEmpty());
    m_readReply->setEnabled(s != S::RunningActions);
    m_copyPrompt->setEnabled(!m_agent->lastPrompt().isEmpty());
    m_pasteReply->setEnabled(s != S::RunningActions);
    m_undo->setEnabled(m_agent->canUndo());
    m_undo->setToolTip(m_agent->canUndo() ? tr("Undo: %1").arg(m_agent->undoLabel())
                                          : tr("Nothing to undo"));

    switch (s) {
    case S::Idle:              m_status->setText(m_agent->taskActive() ? tr("Waiting for you") : tr("Idle")); break;
    case S::WaitingForCopilot: m_status->setText(tr("Waiting for Copilot...")); break;
    case S::RunningActions:    m_status->setText(tr("Step %1: running actions").arg(m_agent->step())); break;
    case S::ResultsReady:      m_status->setText(tr("Results ready: press \"Send results\"")); break;
    }
}

void MainWindow::useTransportForMode()
{
    m_bridge->cancel();
    if (isMode("edge")) {
        m_bridge->setScriptRunner([this](const QString &script, std::function<void(const QVariant &)> done) {
            m_edge->evaluate(script, done);
        });
        m_bridge->setTextInserter([this](const QString &text, std::function<void(bool)> done) {
            m_edge->insertText(text, done);
        });
    } else {
        attachWebEnginePage(m_bridge, m_view->page());
        m_bridge->setTextInserter(nullptr);
    }
}

void MainWindow::deliverPrompt(const QString &text)
{
    if (isMode("builtin")) {
        m_bridge->sendPrompt(text);
        return;
    }
    if (isMode("clipboard")) {
        deliverByClipboard(text);
        return;
    }
    if (m_edge->isConnected()) {
        m_bridge->sendPrompt(text);
        return;
    }
    // Connect first (launching Edge if needed); the prompt goes out then.
    m_queuedPrompt = text;
    m_status->setText(tr("Opening Copilot in Edge..."));
    m_edge->start();
}

void MainWindow::deliverByClipboard(const QString &text)
{
    QApplication::clipboard()->setText(text);
    QApplication::alert(this);
    appendLog(AgentController::LogKind::Info,
              tr("Copied %1 characters to the clipboard. Paste them into Copilot in your browser and send. "
                 "When it has answered, copy the whole reply (its copy button keeps the code blocks) "
                 "and press \"Paste reply\".").arg(text.size()));
    m_status->setText(tr("Waiting for you to paste Copilot's reply"));
}

void MainWindow::pasteReply()
{
    const QString text = QApplication::clipboard()->text();
    if (text.trimmed().isEmpty()) {
        appendLog(AgentController::LogKind::Error, tr("The clipboard is empty. Copy Copilot's reply first."));
        return;
    }
    if (text == m_agent->lastPrompt() || text == m_agent->pendingResults()) {
        appendLog(AgentController::LogKind::Error,
                  tr("The clipboard still holds the message for Copilot, not its reply. Copy the reply first."));
        return;
    }
    m_agent->onReply(AgentProtocol::blocksFromReplyText(text));
}

AgentController::Decision MainWindow::approve(AgentAction &a, QString *note)
{
    QString preview;
    QStringList warnings;
    if (a.type == AgentAction::WriteFile) {
        bool exists = false;
        const QString before = m_agent->workspace().currentContent(a.path, &exists);
        preview = LineDiff::unified(exists ? before : QString(), a.content, a.path);
        warnings = AgentProtocol::writeWarnings(before, exists, a.content);
    } else if (a.type == AgentAction::ReplaceInFile) {
        QString err;
        bool exists = false;
        const QString before = m_agent->workspace().currentContent(a.path, &exists);
        const QString after = m_agent->workspace().applyReplace(a, &err);
        if (!err.isEmpty()) {
            // Nothing to approve: the edit cannot apply. Let it fail and tell Copilot.
            return AgentController::Decision::Approve;
        }
        preview = LineDiff::unified(before, after, a.path);
        // Old text: what is being replaced. Size checks do not apply to a snippet.
        warnings = AgentProtocol::writeWarnings(a.oldText, false, a.newText);
    } else if (a.type == AgentAction::RunCommand) {
        warnings = AgentProtocol::commandWarnings(a.command);
    }

    QApplication::alert(this);
    ApprovalDialog d(a, preview, m_agent->workspace().root(), warnings, this);
    d.exec();
    *note = d.note();
    switch (d.outcome()) {
    case ApprovalDialog::Approved:
        if (a.type == AgentAction::RunCommand && !d.editedCommand().isEmpty() && d.editedCommand() != a.command) {
            appendLog(AgentController::LogKind::Info, tr("You edited the command to: %1").arg(d.editedCommand()));
            a.command = d.editedCommand();
        }
        return AgentController::Decision::Approve;
    case ApprovalDialog::Rejected:
        return AgentController::Decision::Reject;
    case ApprovalDialog::StopAgent:
        m_bridge->cancel();
        return AgentController::Decision::Stop;
    }
    return AgentController::Decision::Reject;
}
