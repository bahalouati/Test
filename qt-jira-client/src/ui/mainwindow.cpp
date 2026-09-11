#include "mainwindow.h"

#include "connectiondialog.h"
#include "core/jiraclient.h"
#include "issuedetailwidget.h"
#include "issuetablemodel.h"
#include "timesheetsettingsdialog.h"
#include "timesheetwidget.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QHeaderView>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>

namespace {

constexpr auto kHistoryKey = "search/history";
constexpr auto kSplitterKey = "window/splitter";
constexpr auto kGeometryKey = "window/geometry";
constexpr int kHistoryLimit = 20;

// Starting points that are useful before anyone has written their own JQL.
const QStringList &defaultQueries()
{
    static const QStringList queries = {
        QStringLiteral("assignee = currentUser() AND resolution = Unresolved ORDER BY updated DESC"),
        QStringLiteral("reporter = currentUser() ORDER BY created DESC"),
        QStringLiteral("assignee = currentUser() AND updated >= -7d ORDER BY updated DESC"),
        QStringLiteral("watcher = currentUser() AND resolution = Unresolved ORDER BY priority DESC"),
    };
    return queries;
}

} // namespace

MainWindow::MainWindow(jira::Client *client, QWidget *parent)
    : QMainWindow(parent)
    , m_client(client)
    , m_model(new IssueTableModel(this))
    , m_proxy(new QSortFilterProxyModel(this))
{
    setupUi();
    loadQueryHistory();

    connect(m_client, &jira::Client::busyChanged, this, [this](bool busy) {
        m_busy->setVisible(busy);
        // Strictly paired: one push per busy period, one pop when it ends.
        if (busy)
            QApplication::setOverrideCursor(Qt::BusyCursor);
        else
            QApplication::restoreOverrideCursor();
    });

    QSettings settings;
    if (!restoreGeometry(settings.value(QLatin1String(kGeometryKey)).toByteArray()))
        resize(1280, 800);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue(QLatin1String(kGeometryKey), saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("JiraDesk"));

    m_jql = new QComboBox(this);
    m_jql->setEditable(true);
    m_jql->setInsertPolicy(QComboBox::NoInsert);
    m_jql->setMinimumWidth(420);
    m_jql->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_jql->lineEdit()->setPlaceholderText(tr("JQL — e.g. project = OPS AND status = \"In Progress\""));
    m_jql->lineEdit()->setClearButtonEnabled(true);
    connect(m_jql->lineEdit(), &QLineEdit::returnPressed, this, &MainWindow::runSearch);

    auto *toolBar = addToolBar(tr("Search"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto *connectAction = toolBar->addAction(tr("Connect…"), this, &MainWindow::showConnectionDialog);
    connectAction->setToolTip(tr("Choose the Jira server and API token"));
    toolBar->addSeparator();
    toolBar->addWidget(m_jql);

    m_searchAction = toolBar->addAction(tr("Search"), this, &MainWindow::runSearch);
    m_searchAction->setShortcut(QKeySequence::Find);
    m_previousAction = toolBar->addAction(tr("◀"), this, &MainWindow::previousPage);
    m_previousAction->setToolTip(tr("Previous page"));
    m_nextAction = toolBar->addAction(tr("▶"), this, &MainWindow::nextPage);
    m_nextAction->setToolTip(tr("Next page"));

    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(IssueTableModel::SortRole);
    m_proxy->setDynamicSortFilter(false);

    m_table = new QTableView(this);
    m_table->setModel(m_proxy);
    m_table->setSortingEnabled(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(IssueTableModel::SummaryColumn, QHeaderView::Stretch);
    m_table->setWordWrap(false);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::issueSelected);

    m_detail = new IssueDetailWidget(m_client, this);
    connect(m_detail, &IssueDetailWidget::issueChanged, this, &MainWindow::refreshIssue);
    connect(m_detail, &IssueDetailWidget::errorOccurred, this, &MainWindow::showError);
    connect(m_detail, &IssueDetailWidget::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 6000);
    });

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_table);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setObjectName(QStringLiteral("mainSplitter"));

    m_timesheet = new TimesheetWidget(m_client, this);
    connect(m_timesheet, &TimesheetWidget::errorOccurred, this, &MainWindow::showError);
    connect(m_timesheet, &TimesheetWidget::issueActivated, this, &MainWindow::openIssueByKey);
    connect(m_timesheet, &TimesheetWidget::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 6000);
    });

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(splitter, tr("Search"));
    m_tabs->addTab(m_timesheet, tr("My month"));
    setCentralWidget(m_tabs);

    QSettings settings;
    if (!splitter->restoreState(settings.value(QLatin1String(kSplitterKey)).toByteArray()))
        splitter->setSizes({720, 520});
    connect(splitter, &QSplitter::splitterMoved, this, [splitter] {
        QSettings().setValue(QLatin1String(kSplitterKey), splitter->saveState());
    });

    m_connectionLabel = new QLabel(tr("Not connected"), this);
    m_resultLabel = new QLabel(this);
    m_busy = new QProgressBar(this);
    m_busy->setRange(0, 0);
    m_busy->setMaximumWidth(120);
    m_busy->setTextVisible(false);
    m_busy->setVisible(false);

    statusBar()->addPermanentWidget(m_resultLabel);
    statusBar()->addPermanentWidget(m_busy);
    statusBar()->addWidget(m_connectionLabel);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(connectAction);
    fileMenu->addAction(tr("&Refresh"), QKeySequence::Refresh, this, &MainWindow::runSearch);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Timesheet settings…"), this, &MainWindow::showTimesheetSettings);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Forget stored token"), this, [this] {
        jira::Credentials::forgetToken();
        statusBar()->showMessage(tr("The stored token was removed. It stays in use until you quit."), 6000);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("About"), this, [this] {
        QMessageBox::about(this,
                           tr("About JiraDesk"),
                           tr("<h3>JiraDesk</h3>"
                              "<p>A Qt desktop client for any Jira reachable over the REST API v2 — "
                              "Jira Server, Data Center or Cloud, at whatever address your instance lives.</p>"
                              "<p>Authenticates with an API token: a Personal Access Token as a bearer "
                              "token on Server/Data Center, or e-mail plus token over HTTP Basic on Cloud.</p>"));
    });

    connect(m_tabs, &QTabWidget::currentChanged, this, [this, toolBar](int index) {
        const bool onSearch = index == 0;
        m_jql->setEnabled(onSearch);
        m_searchAction->setEnabled(onSearch);
        m_previousAction->setVisible(onSearch);
        m_nextAction->setVisible(onSearch);
        toolBar->setVisible(true);
    });

    updatePagingControls();
}

void MainWindow::loadQueryHistory()
{
    QSettings settings;
    QStringList history = settings.value(QLatin1String(kHistoryKey)).toStringList();
    for (const QString &query : defaultQueries()) {
        if (!history.contains(query))
            history.append(query);
    }
    m_jql->addItems(history);
    m_jql->setCurrentIndex(0);
}

void MainWindow::rememberQuery(const QString &jql)
{
    QSettings settings;
    QStringList history = settings.value(QLatin1String(kHistoryKey)).toStringList();
    history.removeAll(jql);
    history.prepend(jql);
    while (history.size() > kHistoryLimit)
        history.removeLast();
    settings.setValue(QLatin1String(kHistoryKey), history);

    const QString current = m_jql->currentText();
    m_jql->blockSignals(true);
    m_jql->clear();
    QStringList combined = history;
    for (const QString &query : defaultQueries()) {
        if (!combined.contains(query))
            combined.append(query);
    }
    m_jql->addItems(combined);
    m_jql->setCurrentText(current);
    m_jql->blockSignals(false);
}

void MainWindow::setInitialQuery(const QString &jql)
{
    m_jql->setCurrentText(jql);
}

void MainWindow::start()
{
    jira::Credentials credentials = jira::Credentials::load();
    m_client->setCredentials(credentials);

    if (!credentials.isComplete()) {
        showConnectionDialog();
        return;
    }
    verifyIdentity();
    runSearch();
}

void MainWindow::showConnectionDialog()
{
    ConnectionDialog dialog(m_client->credentials().isComplete() ? m_client->credentials()
                                                                : jira::Credentials::load(),
                            this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const jira::Credentials credentials = dialog.credentials();
    credentials.save();
    m_client->setCredentials(credentials);

    if (credentials.isComplete()) {
        verifyIdentity();
        runSearch();
    }
}

void MainWindow::verifyIdentity()
{
    const QString host = QUrl(m_client->credentials().baseUrl).host();
    jira::Reply *reply = m_client->fetchMyself();
    connect(reply, &jira::Reply::succeeded, this, [this, host](const QJsonValue &body) {
        const jira::User me = jira::User::fromJson(body.toObject());
        m_connectionLabel->setText(tr("%1 on %2").arg(me.label(), host));
        m_timesheet->setIdentity(me);
        m_timesheet->refresh();
    });
    connect(reply, &jira::Reply::failed, this, [this, host](const jira::Error &error) {
        m_connectionLabel->setText(tr("Not connected to %1").arg(host));
        showError(error.toString());
    });
}

void MainWindow::runSearch()
{
    const QString jql = m_jql->currentText().trimmed();
    if (jql.isEmpty()) {
        statusBar()->showMessage(tr("Enter a JQL query first."), 4000);
        return;
    }
    m_startAt = 0;
    rememberQuery(jql);
    fetchPage(m_startAt);
}

void MainWindow::fetchPage(int startAt)
{
    const QString jql = m_jql->currentText().trimmed();
    if (jql.isEmpty())
        return;

    jira::Reply *reply = m_client->search(jql, startAt, m_pageSize);
    connect(reply, &jira::Reply::succeeded, this, [this, startAt](const QJsonValue &body) {
        const jira::SearchResult result = jira::SearchResult::fromJson(body.toObject());
        m_startAt = startAt;
        m_total = result.total;
        m_model->setIssues(result.issues);
        m_table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
        m_table->resizeColumnsToContents();
        m_table->horizontalHeader()->setSectionResizeMode(IssueTableModel::SummaryColumn, QHeaderView::Stretch);
        m_table->horizontalHeader()->setMinimumSectionSize(60);

        if (result.issues.isEmpty()) {
            m_detail->clear();
            m_resultLabel->setText(tr("No matching issues"));
        } else {
            m_table->selectRow(0);
            m_resultLabel->setText(tr("%1–%2 of %3")
                                           .arg(m_startAt + 1)
                                           .arg(m_startAt + result.issues.size())
                                           .arg(m_total));
        }
        updatePagingControls();
    });
    connect(reply, &jira::Reply::failed, this, [this](const jira::Error &error) {
        // A malformed JQL comes back as a 400 with the offending clause named,
        // which is more useful than anything this client could add.
        showError(error.toString());
        updatePagingControls();
    });
}

void MainWindow::updatePagingControls()
{
    m_previousAction->setEnabled(m_startAt > 0);
    m_nextAction->setEnabled(m_startAt + m_model->rowCount() < m_total);
}

void MainWindow::nextPage()
{
    if (m_startAt + m_model->rowCount() < m_total)
        fetchPage(m_startAt + m_pageSize);
}

void MainWindow::previousPage()
{
    if (m_startAt > 0)
        fetchPage(qMax(0, m_startAt - m_pageSize));
}

void MainWindow::issueSelected(const QModelIndex &current)
{
    if (!current.isValid()) {
        m_detail->clear();
        return;
    }
    m_detail->setIssue(m_model->issueAt(m_proxy->mapToSource(current).row()));
}

void MainWindow::refreshIssue(const QString &issueKey)
{
    jira::Reply *reply = m_client->fetchIssue(issueKey);
    connect(reply, &jira::Reply::succeeded, this, [this, issueKey](const QJsonValue &body) {
        const jira::Issue issue = jira::Issue::fromJson(body.toObject());
        if (issue.isNull())
            return;
        m_model->replaceIssue(issue);
        if (m_detail->currentIssueKey() == issueKey)
            m_detail->setIssue(issue);
    });
    connect(reply, &jira::Reply::failed, this, [](const jira::Error &) {
        // The write already succeeded; a stale row is not worth a dialog.
    });
}

void MainWindow::showTimesheetSettings()
{
    TimesheetSettingsDialog dialog(jira::TimesheetSettings::load(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    dialog.settings().save();
    m_timesheet->refresh();
}

void MainWindow::openIssueByKey(const QString &issueKey)
{
    // Jumping from a work log row back to the issue it was booked against.
    m_tabs->setCurrentIndex(0);
    m_jql->setCurrentText(QStringLiteral("key = %1").arg(issueKey));
    runSearch();
}

void MainWindow::showError(const QString &message)
{
    statusBar()->showMessage(message.section(QLatin1Char('\n'), 0, 0), 8000);
    QMessageBox::warning(this, tr("Jira"), message);
}
