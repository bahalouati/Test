#include "mainwindow.h"

#include "agentprotocol.h"
#include "approvaldialog.h"
#include "copilotbridge.h"
#include "linediff.h"
#include "officefile.h"
#include "edgesession.h"
#include "jiraissue.h"
#include "jiraconnection.h"
#include "dialogs.h"
#include "webenginetransport.h"
#include "webpage.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListWidget>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUrl>
#include <QInputDialog>
#include <QProcess>
#include <QDialogButtonBox>
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

#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Copilot Agent"));
    m_settings = AppSettings::load();
    m_agent = new AgentController(this);
    m_agent->setApprover([this](AgentAction &a, QString *note) { return approve(a, note); });
    m_jira = new JiraConn::Client(this);

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
    {
        const QSignalBlocker b1(m_mode), b2(m_planFirst);
        m_mode->setCurrentIndex(qBound(0, QSettings().value(QStringLiteral("taskMode"), 0).toInt(), 2));
        m_planFirst->setChecked(QSettings().value(QStringLiteral("planFirst"), false).toBool());
    }
    applyMode();
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
    m_describe = appBar->addAction(tr("Copy page details"));
    m_describe->setToolTip(tr("If the app misses Copilot's replies: copies how the page is built, "
                              "to send to whoever maintains this app"));
    connect(m_describe, &QAction::triggered, this, [this] {
        m_bridge->describePage([this](const QString &text) {
            QApplication::clipboard()->setText(text);
            appendLog(AgentController::LogKind::Info,
                      tr("Copied the page details (%1 characters). They include the text of the last replies; "
                         "check before sharing.").arg(text.size()));
        });
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

    // What kind of work: a free task, a Jira ticket, or a review.
    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(tr("Mode:"), taskBox));
    m_mode = new QComboBox(taskBox);
    m_mode->addItem(tr("Task"), int(TaskMode));
    m_mode->addItem(tr("Jira issue"), int(JiraMode));
    m_mode->addItem(tr("Review"), int(ReviewMode));
    m_mode->setToolTip(tr("Task: describe what to do. Jira issue: the ticket is the task. Review: read-only review of changes."));
    modeRow->addWidget(m_mode);
    m_planFirst = new QCheckBox(tr("Plan first"), taskBox);
    m_planFirst->setToolTip(tr("Copilot only reads and searches, then proposes a plan; it can change things once you approve it"));
    modeRow->addSpacing(12);
    modeRow->addWidget(m_planFirst);
    modeRow->addStretch();
    tl->addLayout(modeRow);

    m_jiraRow = new QWidget(taskBox);
    auto *jl = new QVBoxLayout(m_jiraRow);
    jl->setContentsMargins(0, 0, 0, 0);
    auto *jr = new QHBoxLayout;
    m_jiraKey = new QLineEdit(m_jiraRow);
    m_jiraKey->setPlaceholderText(tr("Ticket key, e.g. PROJ-123 (or paste its link)"));
    auto *loadBtn = new QPushButton(tr("Load"), m_jiraRow);
    auto *jiraSettings = new QPushButton(tr("Jira settings..."), m_jiraRow);
    jr->addWidget(m_jiraKey, 1);
    jr->addWidget(loadBtn);
    jr->addWidget(jiraSettings);
    jl->addLayout(jr);
    m_jiraInfo = new QLabel(m_jiraRow);
    m_jiraInfo->setWordWrap(true);
    m_jiraInfo->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_jiraInfo->setOpenExternalLinks(true);
    jl->addWidget(m_jiraInfo);
    tl->addWidget(m_jiraRow);
    connect(loadBtn, &QPushButton::clicked, this, [this] {
        const QString key = JiraConn::issueKeyFrom(m_jiraKey->text());
        if (key.isEmpty()) {
            appendLog(AgentController::LogKind::Error, tr("That is not a Jira issue key like PROJ-123."));
            return;
        }
        loadJira(key, [] {});
    });
    connect(m_jiraKey, &QLineEdit::returnPressed, loadBtn, &QPushButton::click);
    connect(jiraSettings, &QPushButton::clicked, this, &MainWindow::openJiraSettings);

    m_reviewRow = new QWidget(taskBox);
    auto *rr = new QHBoxLayout(m_reviewRow);
    rr->setContentsMargins(0, 0, 0, 0);
    rr->addWidget(new QLabel(tr("Review:"), m_reviewRow));
    m_reviewTarget = new QComboBox(m_reviewRow);
    m_reviewTarget->addItem(tr("Uncommitted changes"), int(Git::Target::Uncommitted));
    m_reviewTarget->addItem(tr("Last commit"), int(Git::Target::LastCommit));
    m_reviewTarget->addItem(tr("This branch compared with"), int(Git::Target::Branch));
    rr->addWidget(m_reviewTarget);
    m_reviewBase = new QLineEdit(m_reviewRow);
    m_reviewBase->setPlaceholderText(tr("main"));
    m_reviewBase->setMaximumWidth(160);
    rr->addWidget(m_reviewBase);
    rr->addStretch();
    tl->addWidget(m_reviewRow);
    connect(m_reviewTarget, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_reviewBase->setVisible(m_reviewTarget->currentData().toInt() == int(Git::Target::Branch));
    });

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

    // Files picked by hand: dragged in, or chosen with the button.
    auto *filesBox = new QGroupBox(tr("Files (optional): drag files here"), panel);
    auto *fbl = new QVBoxLayout(filesBox);
    m_files = new QListWidget(filesBox);
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setMaximumHeight(96);
    m_files->setToolTip(tr("Files for this task. Their content goes to Copilot with the task. "
                           "Drag files from Explorer onto this list or anywhere on the window."));
    m_files->setAcceptDrops(true);
    m_files->viewport()->setAcceptDrops(true);
    m_files->viewport()->installEventFilter(this);
    fbl->addWidget(m_files);
    auto *frow = new QHBoxLayout;
    auto *addFilesBtn = new QPushButton(tr("Add files..."), filesBox);
    m_removeFiles = new QPushButton(tr("Remove"), filesBox);
    auto *clearFiles = new QPushButton(tr("Clear"), filesBox);
    m_onlyFiles = new QCheckBox(tr("Only let Copilot change these files"), filesBox);
    m_onlyFiles->setChecked(true);
    m_onlyFiles->setEnabled(false);   // until files are added
    m_onlyFiles->setToolTip(tr("Copilot may still read and search everything, but writes and edits to any "
                               "other file are refused automatically"));
    frow->addWidget(addFilesBtn);
    frow->addWidget(m_removeFiles);
    frow->addWidget(clearFiles);
    frow->addStretch();
    frow->addWidget(m_onlyFiles);
    fbl->addLayout(frow);
    pl->addWidget(filesBox);

    connect(addFilesBtn, &QPushButton::clicked, this, [this] {
        const QStringList picked = QFileDialog::getOpenFileNames(this, tr("Files for this task"),
                                                                 m_agent->workspace().root());
        addFiles(picked);
    });
    connect(m_removeFiles, &QPushButton::clicked, this, [this] {
        qDeleteAll(m_files->selectedItems());
        syncFocusFiles();
    });
    connect(clearFiles, &QPushButton::clicked, this, [this] {
        m_files->clear();
        syncFocusFiles();
    });
    connect(m_files, &QListWidget::itemSelectionChanged, this, &MainWindow::updateButtons);
    connect(m_onlyFiles, &QCheckBox::toggled, this, &MainWindow::syncFocusFiles);

    // Dropping a file on the task box would paste its path as text; take it
    // as a picked file instead.
    m_task->viewport()->installEventFilter(this);
    setAcceptDrops(true);

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

    connect(m_start, &QPushButton::clicked, this, &MainWindow::startClicked);
    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        QSettings().setValue(QStringLiteral("taskMode"), m_mode->currentIndex());
        applyMode();
    });
    connect(m_planFirst, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("planFirst"), on); });
    m_agent->setPlanApprover([this](const QString &plan, QString *note) {
        QApplication::alert(this);
        PlanDialog d(plan, this);
        d.exec();
        *note = d.note();
        if (d.decision() == AgentController::PlanDecision::Stop)
            m_bridge->cancel();
        return d.decision();
    });
    connect(m_agent, &AgentController::findingsReported, this, [this](const QJsonArray &findings) {
        appendLog(AgentController::LogKind::Done, tr("Review findings: %1. The list is open in its own window.").arg(findings.size()));
        auto *d = new FindingsDialog(findings, m_reviewDescription.isEmpty() ? tr("the changes") : m_reviewDescription, this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
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
        if (!m_activeJiraKey.isEmpty())
            QTimer::singleShot(0, this, [this, summary] { jiraWrapUp(summary); });
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
    m_describe->setVisible(!isMode("clipboard"));
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
    if (m_files && m_agent->workspace().root() != QFileInfo(path).canonicalFilePath()) {
        m_files->clear();   // picked files belong to the old folder
        syncFocusFiles();
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
    if (m_removeFiles)
        m_removeFiles->setEnabled(!m_files->selectedItems().isEmpty());
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
        m_edge->wake();
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
    } else if (a.type == AgentAction::SetCells
               || (a.type == AgentAction::ReplaceInFile && Office::kindOf(a.path) != Office::None)) {
        // Word/Excel: preview the readable text before and after.
        QByteArray bytes;
        QString before, after, err, note;
        if (!m_agent->workspace().officeEdit(a, &bytes, &before, &after, &err, &note))
            return AgentController::Decision::Approve;   // cannot apply: let it fail and tell Copilot
        preview = LineDiff::unified(before, after, a.path);
        if (!note.isEmpty())
            warnings << note.left(1).toUpper() + note.mid(1) + QLatin1Char('.');
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

static QStringList localFiles(const QMimeData *mime)
{
    QStringList out;
    if (!mime || !mime->hasUrls())
        return out;
    for (const QUrl &u : mime->urls())
        if (u.isLocalFile())
            out << u.toLocalFile();
    return out;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (!localFiles(e->mimeData()).isEmpty())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    const QStringList files = localFiles(e->mimeData());
    if (files.isEmpty())
        return;
    e->acceptProposedAction();
    addFiles(files);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_files->viewport() || watched == m_task->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *e = static_cast<QDragMoveEvent *>(event);
            if (!localFiles(e->mimeData()).isEmpty()) {
                e->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *e = static_cast<QDropEvent *>(event);
            const QStringList files = localFiles(e->mimeData());
            if (!files.isEmpty()) {
                e->acceptProposedAction();
                addFiles(files);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::addFiles(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    // No project folder yet: use the folder of the first file.
    if (!m_agent->workspace().isValid()) {
        const QFileInfo first(paths.first());
        setFolder(first.isDir() ? first.absoluteFilePath() : first.absolutePath());
        appendLog(AgentController::LogKind::Info,
                  tr("Project folder set to %1. Change it with \"Choose...\" if the project is a level up.")
                      .arg(QDir::toNativeSeparators(m_agent->workspace().root())));
    }
    const QDir root(m_agent->workspace().root());
    QStringList skipped;
    int added = 0;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            skipped << tr("%1 (a folder; pick the files inside it)").arg(fi.fileName());
            continue;
        }
        const QString canon = fi.canonicalFilePath();
        const QString rel = root.relativeFilePath(canon.isEmpty() ? fi.absoluteFilePath() : canon);
        if (canon.isEmpty() || rel.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(rel)) {
            skipped << tr("%1 (outside the project folder)").arg(QDir::toNativeSeparators(p));
            continue;
        }
        if (!m_files->findItems(rel, Qt::MatchExactly).isEmpty())
            continue;
        auto *item = new QListWidgetItem(rel, m_files);
        item->setToolTip(QDir::toNativeSeparators(canon));
        ++added;
    }
    if (!skipped.isEmpty())
        appendLog(AgentController::LogKind::Error, tr("Not added: %1").arg(skipped.join(QStringLiteral("; "))));
    if (added)
        syncFocusFiles();
}

void MainWindow::syncFocusFiles()
{
    QStringList rels;
    for (int i = 0; i < m_files->count(); ++i)
        rels << m_files->item(i)->text();
    m_agent->setFocusFiles(rels, m_onlyFiles->isChecked());
    m_onlyFiles->setEnabled(!rels.isEmpty());
    updateButtons();
}

// ------------------------------------------------------------------ modes

MainWindow::Mode MainWindow::mode() const
{
    return Mode(m_mode->currentData().toInt());
}

void MainWindow::applyMode()
{
    const Mode m = mode();
    m_jiraRow->setVisible(m == JiraMode);
    m_reviewRow->setVisible(m == ReviewMode);
    m_reviewBase->setVisible(m_reviewTarget->currentData().toInt() == int(Git::Target::Branch));
    m_planFirst->setVisible(m != ReviewMode);
    switch (m) {
    case TaskMode:
        m_start->setText(tr("Start task"));
        m_task->setPlaceholderText(tr("What should Copilot build or change?\n"
                                      "e.g. \"Add a --verbose flag to the CLI and a test for it.\"\n\n"
                                      "While a task runs, type here and press Send to answer Copilot."));
        break;
    case JiraMode:
        m_start->setText(tr("Start ticket"));
        m_task->setPlaceholderText(tr("Optional: your own notes for this ticket (scope, hints, what to skip).\n\n"
                                      "While it runs, type here and press Send to answer Copilot."));
        break;
    case ReviewMode:
        m_start->setText(tr("Start review"));
        m_task->setPlaceholderText(tr("Optional: what to focus on (e.g. \"thread safety\", \"the new parser\").\n\n"
                                      "Nothing is changed in a review: Copilot only reads."));
        break;
    }
}

void MainWindow::startClicked()
{
    switch (mode()) {
    case TaskMode:   startPlainTask(); break;
    case JiraMode:   startJiraTask(); break;
    case ReviewMode: startReview(); break;
    }
}

void MainWindow::startPlainTask()
{
    m_activeJiraKey.clear();
    m_agent->setPlanFirst(m_planFirst->isChecked());
    m_agent->startTask(m_task->toPlainText());
    if (m_agent->taskActive())
        m_task->clear();
}

// ------------------------------------------------------------------- Jira

void MainWindow::openJiraSettings()
{
    JiraSettingsDialog d(JiraConn::Credentials::load(), this);
    if (d.exec() == QDialog::Accepted) {
        d.credentials().save();
        appendLog(AgentController::LogKind::Info, tr("Jira settings saved."));
    }
}

void MainWindow::loadJira(const QString &key, std::function<void()> then)
{
    JiraConn::Credentials creds = JiraConn::Credentials::load();
    if (!creds.isComplete()) {
        openJiraSettings();
        creds = JiraConn::Credentials::load();
        if (!creds.isComplete()) {
            appendLog(AgentController::LogKind::Error, tr("Jira is not set up yet: open \"Jira settings...\"."));
            return;
        }
    }
    m_jira->setCredentials(creds);
    m_jiraInfo->setText(tr("Loading %1...").arg(key));
    m_jira->fetchIssue(key, [this, key, then](const QJsonValue &body, const QString &error) {
        if (!error.isEmpty()) {
            m_jiraInfo->setText(tr("Could not load %1.").arg(key));
            appendLog(AgentController::LogKind::Error, tr("Jira: %1").arg(error));
            return;
        }
        m_jiraIssue = body.toObject();
        m_jiraAttachments.clear();
        const JiraIssue::Brief b = JiraIssue::briefOf(m_jiraIssue);
        const QString link = m_jira->browseUrl(b.key).toString();
        m_jiraInfo->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(link.toHtmlEscaped(), b.line().toHtmlEscaped()));

        // Readable attachments (Word, Excel, text) are downloaded and their
        // text goes into the task.
        const QVector<JiraIssue::Attachment> atts = JiraIssue::readableAttachments(m_jiraIssue);
        if (atts.isEmpty()) {
            appendLog(AgentController::LogKind::Info, tr("Loaded %1.").arg(b.line()));
            then();
            return;
        }
        auto remaining = std::make_shared<int>(int(atts.size()));
        for (const JiraIssue::Attachment &a : atts) {
            m_jira->download(a.url, 5 * 1024 * 1024, [this, a, remaining, b, then](const QByteArray &bytes, const QString &err) {
                if (err.isEmpty())
                    m_jiraAttachments.insert(a.name, JiraIssue::attachmentText(a.name, bytes));
                else
                    appendLog(AgentController::LogKind::Error, tr("Attachment %1 not read: %2").arg(a.name, err));
                if (--*remaining == 0) {
                    appendLog(AgentController::LogKind::Info, tr("Loaded %1 with %2 readable attachment(s).")
                                  .arg(b.line()).arg(m_jiraAttachments.size()));
                    then();
                }
            });
        }
    });
}

void MainWindow::startJiraTask()
{
    const QString key = JiraConn::issueKeyFrom(m_jiraKey->text());
    if (key.isEmpty()) {
        appendLog(AgentController::LogKind::Error, tr("Enter a Jira issue key like PROJ-123 first."));
        m_jiraKey->setFocus();
        return;
    }
    if (!m_agent->workspace().isValid()) {
        appendLog(AgentController::LogKind::Error, tr("Choose a project folder first."));
        return;
    }
    auto go = [this, key] {
        if (!createJiraBranch())
            return;
        m_activeJiraKey = key;
        m_agent->setPlanFirst(m_planFirst->isChecked());
        m_agent->startTask(JiraIssue::toTask(m_jiraIssue, m_jiraAttachments, m_task->toPlainText()));
        if (m_agent->taskActive())
            m_task->clear();
    };
    if (JiraIssue::briefOf(m_jiraIssue).key == key)
        go();
    else
        loadJira(key, go);
}

static Git::Result runShell(const QString &command, const QString &dir)
{
    // The user may have edited the command in the approval dialog: run it as typed.
    Git::Result r;
    QProcess p;
    p.setWorkingDirectory(dir);
    p.setStandardInputFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    p.setProgram(QStringLiteral("cmd.exe"));
    p.setNativeArguments(QStringLiteral("/D /S /C \"") + command + QLatin1Char('"'));
#else
    p.setProgram(QStringLiteral("/bin/sh"));
    p.setArguments({QStringLiteral("-c"), command});
#endif
    p.start();
    if (!p.waitForFinished(30000)) {
        p.kill();
        r.err = QStringLiteral("timed out");
        return r;
    }
    r.exitCode = p.exitCode();
    r.out = QString::fromLocal8Bit(p.readAllStandardOutput());
    r.err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
    r.ok = r.exitCode == 0;
    return r;
}

bool MainWindow::createJiraBranch()
{
    const QString root = m_agent->workspace().root();
    m_jiraBranch.clear();
    if (!Git::isRepo(root)) {
        appendLog(AgentController::LogKind::Info, tr("The project folder is not a git repository, so no branch was created."));
        return true;
    }
    const JiraIssue::Brief b = JiraIssue::briefOf(m_jiraIssue);
    const QString name = Git::branchNameFor(b.key, b.summary);
    if (Git::currentBranch(root) == name) {
        m_jiraBranch = name;
        appendLog(AgentController::LogKind::Info, tr("Already on branch %1.").arg(name));
        return true;
    }
    AgentAction cmd;
    cmd.type = AgentAction::RunCommand;
    cmd.command = Git::branchExists(root, name) ? QStringLiteral("git checkout ") + name
                                                : QStringLiteral("git checkout -b ") + name;
    QString note;
    const AgentController::Decision d = approve(cmd, &note);
    if (d == AgentController::Decision::Stop)
        return false;
    if (d == AgentController::Decision::Reject) {
        appendLog(AgentController::LogKind::Info, tr("No branch created; working on %1.").arg(Git::currentBranch(root)));
        return true;
    }
    const Git::Result r = runShell(cmd.command, root);
    if (!r.ok) {
        appendLog(AgentController::LogKind::Error, tr("The branch command failed: %1").arg(r.err.isEmpty() ? r.out : r.err));
        return QMessageBox::question(this, tr("Branch"), tr("Creating the branch failed. Start the ticket anyway on %1?")
                                         .arg(Git::currentBranch(root))) == QMessageBox::Yes;
    }
    m_jiraBranch = Git::currentBranch(root);
    appendLog(AgentController::LogKind::Result, tr("On branch %1.").arg(m_jiraBranch));
    return true;
}

void MainWindow::jiraWrapUp(const QString &summary)
{
    const QString key = m_activeJiraKey;
    m_activeJiraKey.clear();
    if (key.isEmpty())
        return;

    // 1. A comment on the ticket, edited by the user before it is posted.
    QDialog d(this);
    d.setWindowTitle(tr("Post to %1?").arg(key));
    d.resize(640, 420);
    auto *lay = new QVBoxLayout(&d);
    auto *label = new QLabel(tr("The task is done. Post this comment on %1? Edit it first if you like.").arg(key), &d);
    label->setWordWrap(true);
    lay->addWidget(label);
    auto *text = new QPlainTextEdit(JiraIssue::wrapUpComment(summary, m_agent->changedFiles(), m_jiraBranch), &d);
    lay->addWidget(text, 1);
    auto *buttons = new QDialogButtonBox(&d);
    auto *post = buttons->addButton(tr("Post comment"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Skip"), QDialogButtonBox::RejectRole);
    post->setDefault(true);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted || text->toPlainText().trimmed().isEmpty()) {
        appendLog(AgentController::LogKind::Info, tr("Nothing posted to %1.").arg(key));
        return;
    }
    m_jira->addComment(key, text->toPlainText().trimmed(), [this, key](const QJsonValue &, const QString &error) {
        if (!error.isEmpty()) {
            appendLog(AgentController::LogKind::Error, tr("Could not post the comment on %1: %2").arg(key, error));
            return;
        }
        appendLog(AgentController::LogKind::Result, tr("Comment posted on %1.").arg(key));

        // 2. Optionally move the ticket on, using the transitions Jira allows.
        m_jira->fetchTransitions(key, [this, key](const QVector<JiraConn::Transition> &ts, const QString &err) {
            if (!err.isEmpty() || ts.isEmpty()) {
                if (!err.isEmpty())
                    appendLog(AgentController::LogKind::Error, tr("Could not read %1's possible statuses: %2").arg(key, err));
                return;
            }
            QStringList items{tr("Keep the current status")};
            for (const JiraConn::Transition &t : ts)
                items << (t.toStatus.isEmpty() || t.toStatus == t.name ? t.name : QStringLiteral("%1 (to %2)").arg(t.name, t.toStatus));
            bool ok = false;
            const QString choice = QInputDialog::getItem(this, tr("Move %1?").arg(key),
                                                         tr("Move the ticket to another status?"), items, 0, false, &ok);
            const int index = items.indexOf(choice) - 1;
            if (!ok || index < 0)
                return;
            const JiraConn::Transition t = ts.at(index);
            m_jira->applyTransition(key, t.id, [this, key, t](const QJsonValue &, const QString &e) {
                if (!e.isEmpty())
                    appendLog(AgentController::LogKind::Error, tr("Could not move %1: %2").arg(key, e));
                else
                    appendLog(AgentController::LogKind::Result, tr("%1 moved: %2.").arg(key, t.name));
            });
        });
    });
}

// ----------------------------------------------------------------- review

Git::Target MainWindow::reviewTarget() const
{
    Git::Target t;
    t.kind = Git::Target::Kind(m_reviewTarget->currentData().toInt());
    if (t.kind == Git::Target::Branch) {
        t.base = m_reviewBase->text().trimmed();
        if (t.base.isEmpty())
            t.base = Git::defaultBase(m_agent->workspace().root());
    }
    return t;
}

void MainWindow::startReview()
{
    const QString root = m_agent->workspace().root();
    if (!m_agent->workspace().isValid()) {
        appendLog(AgentController::LogKind::Error, tr("Choose a project folder first."));
        return;
    }
    if (!Git::isRepo(root)) {
        appendLog(AgentController::LogKind::Error, tr("A review compares changes with git, and this project folder is not a git repository."));
        return;
    }
    const Git::Target target = reviewTarget();
    if (target.kind == Git::Target::Branch && target.base.isEmpty()) {
        appendLog(AgentController::LogKind::Error, tr("Enter the branch to compare with (e.g. main)."));
        m_reviewBase->setFocus();
        return;
    }
    QString err;
    const QString stat = Git::diffStat(root, target, &err);
    if (!err.isEmpty()) {
        appendLog(AgentController::LogKind::Error, tr("git: %1").arg(err));
        return;
    }
    const QString fullDiff = Git::diff(root, target);
    const QStringList untracked = target.kind == Git::Target::Uncommitted ? Git::untrackedFiles(root) : QStringList();
    if (fullDiff.trimmed().isEmpty() && untracked.isEmpty()) {
        appendLog(AgentController::LogKind::Info, tr("Nothing to review: %1 has no changes.").arg(target.describe()));
        return;
    }
    m_activeJiraKey.clear();
    m_reviewDescription = target.describe();
    m_agent->setDiffTarget(target);
    m_agent->startReview(AgentProtocol::reviewPrompt(target.describe(), QFileInfo(root).fileName(), stat, fullDiff,
                                                     untracked, m_task->toPlainText()),
                         target);
    if (m_agent->taskActive())
        m_task->clear();
}
