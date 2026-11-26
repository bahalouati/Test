#include "mainwindow.h"

#include "agentprotocol.h"
#include "approvaldialog.h"
#include "copilotbridge.h"
#include "linediff.h"
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
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineView>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Copilot Agent"));
    if (const QScreen *scr = QGuiApplication::primaryScreen()) {
        const QSize avail = scr->availableSize();
        resize(qMin(1500, avail.width() * 9 / 10), qMin(950, avail.height() * 9 / 10));
    }
    m_settings = AppSettings::load();
    m_agent = new AgentController(this);
    m_agent->setApprover([this](AgentAction &a, QString *note) { return approve(a, note); });

    buildUi();
    wire();
    applySettings();

    const QString lastFolder = QSettings().value(QStringLiteral("workspace")).toString();
    if (!lastFolder.isEmpty() && QDir(lastFolder).exists())
        setFolder(lastFolder);
    if (!m_settings.externalBrowser)
        m_view->load(QUrl(m_settings.url));
    else
        appendLog(AgentController::LogKind::Info,
                  tr("External-browser mode: open Copilot (%1) in your browser. Each message is copied "
                     "to the clipboard for you to paste; copy Copilot's reply back and press \"Paste reply\". "
                     "Change this in Settings.").arg(m_settings.url));
    updateButtons();

    QSettings q;
    restoreGeometry(q.value(QStringLiteral("geometry")).toByteArray());
}

MainWindow::~MainWindow()
{
    QSettings().setValue(QStringLiteral("geometry"), saveGeometry());
}

void MainWindow::buildUi()
{
    // Left: the real Copilot page. Right: the agent panel.
    QWebEngineProfile *profile = WebPage::createProfile(this);
    m_view = new QWebEngineView(this);
    m_view->setPage(new WebPage(profile, m_view));
    m_bridge = new CopilotBridge(m_view->page(), this);

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
        if (!m_settings.externalBrowser && (oldUrl != m_settings.url || m_view->url().isEmpty()))
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
    pl->addLayout(manual);

    m_log = new QTextBrowser(panel);
    m_log->setOpenLinks(false);
    pl->addWidget(m_log, 1);

    auto *split = new QSplitter(this);
    split->addWidget(m_view);
    split->addWidget(panel);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({width() * 3 / 5, width() * 2 / 5});
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
    // In external-browser mode the built-in page is not used at all.
    m_view->setVisible(!m_settings.externalBrowser);
    m_browserBar->setVisible(!m_settings.externalBrowser);
    m_readReply->setVisible(!m_settings.externalBrowser);
    if (m_settings.externalBrowser)
        m_bridge->cancel();
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

    switch (s) {
    case S::Idle:              m_status->setText(m_agent->taskActive() ? tr("Waiting for you") : tr("Idle")); break;
    case S::WaitingForCopilot: m_status->setText(tr("Waiting for Copilot...")); break;
    case S::RunningActions:    m_status->setText(tr("Step %1: running actions").arg(m_agent->step())); break;
    case S::ResultsReady:      m_status->setText(tr("Results ready: press \"Send results\"")); break;
    }
}

void MainWindow::deliverPrompt(const QString &text)
{
    if (!m_settings.externalBrowser) {
        m_bridge->sendPrompt(text);
        return;
    }
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
    if (a.type == AgentAction::WriteFile) {
        bool exists = false;
        const QString before = m_agent->workspace().currentContent(a.path, &exists);
        preview = LineDiff::unified(exists ? before : QString(), a.content, a.path);
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
    }

    QApplication::alert(this);
    ApprovalDialog d(a, preview, m_agent->workspace().root(), this);
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
