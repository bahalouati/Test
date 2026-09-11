#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "connectiondialog.h"
#include "core/jiraclient.h"
#include "issuedetailwidget.h"
#include "issuetablemodel.h"
#include "sprintwidget.h"
#include "timesheetsettingsdialog.h"
#include "timesheetwidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QToolButton>

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
    , ui(new Ui::MainWindow)
    , m_client(client)
    , m_model(new IssueTableModel(this))
    , m_proxy(new QSortFilterProxyModel(this))
{
    ui->setupUi(this);

    // The promoted widgets are built by setupUi with only a parent, so they are
    // handed the client here.
    ui->detail->setClient(client);
    ui->sprint->setClient(client);
    ui->timesheet->setClient(client);

    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(IssueTableModel::SortRole);
    m_proxy->setDynamicSortFilter(false);
    ui->issueTable->setModel(m_proxy);
    ui->issueTable->horizontalHeader()->setSectionResizeMode(IssueTableModel::SummaryColumn,
                                                             QHeaderView::Stretch);

    ui->jql->lineEdit()->setPlaceholderText(
            tr("JQL — e.g. project = OPS AND status = \"In Progress\""));
    ui->jql->lineEdit()->setClearButtonEnabled(true);

    // The busy indicator belongs in the status bar, which the .ui cannot place.
    auto *busy = new QProgressBar(this);
    busy->setObjectName(QStringLiteral("busy"));
    busy->setRange(0, 0);
    busy->setMaximumWidth(120);
    busy->setTextVisible(false);
    busy->setVisible(false);

    auto *connectionLabel = new QLabel(tr("Not connected"), this);
    connectionLabel->setObjectName(QStringLiteral("connectionLabel"));
    auto *resultLabel = new QLabel(this);
    resultLabel->setObjectName(QStringLiteral("resultLabel"));

    statusBar()->addPermanentWidget(resultLabel);
    statusBar()->addPermanentWidget(busy);
    statusBar()->addWidget(connectionLabel);

    connectUi();
    loadQueryHistory();

    QSettings settings;
    if (!restoreGeometry(settings.value(QLatin1String(kGeometryKey)).toByteArray()))
        resize(1280, 800);
    if (!ui->splitter->restoreState(settings.value(QLatin1String(kSplitterKey)).toByteArray()))
        ui->splitter->setSizes({720, 520});

    updatePagingControls();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::connectUi()
{
    connect(ui->actionConnect, &QAction::triggered, this, &MainWindow::showConnectionDialog);
    connect(ui->actionRefresh, &QAction::triggered, this, &MainWindow::runSearch);
    connect(ui->actionTimesheetSettings, &QAction::triggered, this,
            &MainWindow::showTimesheetSettings);
    connect(ui->actionQuit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::showAbout);
    connect(ui->actionForgetToken, &QAction::triggered, this, [this] {
        jira::Credentials::forgetToken();
        statusBar()->showMessage(tr("The stored token was removed. It stays in use until you quit."),
                                 6000);
    });

    connect(ui->search, &QPushButton::clicked, this, &MainWindow::runSearch);
    connect(ui->previousPage, &QToolButton::clicked, this, &MainWindow::previousPage);
    connect(ui->nextPage, &QToolButton::clicked, this, &MainWindow::nextPage);
    connect(ui->jql->lineEdit(), &QLineEdit::returnPressed, this, &MainWindow::runSearch);

    connect(ui->issueTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::issueSelected);

    connect(ui->detail, &IssueDetailWidget::issueChanged, this, &MainWindow::refreshIssue);
    connect(ui->detail, &IssueDetailWidget::errorOccurred, this, &MainWindow::showError);
    connect(ui->detail, &IssueDetailWidget::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 6000);
    });

    connect(ui->sprint, &SprintWidget::errorOccurred, this, &MainWindow::showError);
    connect(ui->sprint, &SprintWidget::issueActivated, this, &MainWindow::openIssueByKey);
    connect(ui->sprint, &SprintWidget::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 6000);
    });

    connect(ui->timesheet, &TimesheetWidget::errorOccurred, this, &MainWindow::showError);
    connect(ui->timesheet, &TimesheetWidget::issueActivated, this, &MainWindow::openIssueByKey);
    connect(ui->timesheet, &TimesheetWidget::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 6000);
    });

    connect(ui->splitter, &QSplitter::splitterMoved, this, [this] {
        QSettings().setValue(QLatin1String(kSplitterKey), ui->splitter->saveState());
    });

    connect(m_client, &jira::Client::busyChanged, this, [this](bool busy) {
        if (auto *bar = findChild<QProgressBar *>(QStringLiteral("busy")))
            bar->setVisible(busy);
        // Strictly paired: one push per busy period, one pop when it ends.
        if (busy)
            QApplication::setOverrideCursor(Qt::BusyCursor);
        else
            QApplication::restoreOverrideCursor();
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue(QLatin1String(kGeometryKey), saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::loadQueryHistory()
{
    QSettings settings;
    QStringList history = settings.value(QLatin1String(kHistoryKey)).toStringList();
    for (const QString &query : defaultQueries()) {
        if (!history.contains(query))
            history.append(query);
    }
    ui->jql->addItems(history);
    ui->jql->setCurrentIndex(0);
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

    const QString current = ui->jql->currentText();
    ui->jql->blockSignals(true);
    ui->jql->clear();
    QStringList combined = history;
    for (const QString &query : defaultQueries()) {
        if (!combined.contains(query))
            combined.append(query);
    }
    ui->jql->addItems(combined);
    ui->jql->setCurrentText(current);
    ui->jql->blockSignals(false);
}

void MainWindow::setInitialQuery(const QString &jql)
{
    ui->jql->setCurrentText(jql);
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

void MainWindow::showAbout()
{
    QMessageBox::about(this,
                       tr("About JiraDesk"),
                       tr("<h3>JiraDesk</h3>"
                          "<p>A Qt desktop client for any Jira reachable over the REST API v2 — "
                          "Jira Server, Data Center or Cloud, at whatever address your instance lives.</p>"
                          "<p>Authenticates with an API token: a Personal Access Token as a bearer "
                          "token on Server/Data Center, or a user name plus password or token over "
                          "HTTP Basic.</p>"));
}

void MainWindow::verifyIdentity()
{
    const QString host = QUrl(m_client->credentials().baseUrl).host();
    auto *connectionLabel = findChild<QLabel *>(QStringLiteral("connectionLabel"));

    jira::Reply *reply = m_client->fetchMyself();
    connect(reply, &jira::Reply::succeeded, this, [this, host, connectionLabel](const QJsonValue &body) {
        const jira::User me = jira::User::fromJson(body.toObject());
        if (connectionLabel)
            connectionLabel->setText(tr("%1 on %2").arg(me.label(), host));
        ui->timesheet->setIdentity(me);
        ui->timesheet->refresh();
        ui->sprint->setIdentity(me);
        ui->sprint->refresh();
    });
    connect(reply, &jira::Reply::failed, this, [this, host, connectionLabel](const jira::Error &error) {
        if (connectionLabel)
            connectionLabel->setText(tr("Not connected to %1").arg(host));
        showError(error.toString());
    });
}

void MainWindow::runSearch()
{
    const QString jql = ui->jql->currentText().trimmed();
    if (jql.isEmpty()) {
        statusBar()->showMessage(tr("Enter a JQL query first."), 4000);
        return;
    }
    ui->tabs->setCurrentWidget(ui->searchTab);
    m_startAt = 0;
    rememberQuery(jql);
    fetchPage(m_startAt);
}

void MainWindow::fetchPage(int startAt)
{
    const QString jql = ui->jql->currentText().trimmed();
    if (jql.isEmpty())
        return;

    auto *resultLabel = findChild<QLabel *>(QStringLiteral("resultLabel"));

    jira::Reply *reply = m_client->search(jql, startAt, m_pageSize);
    connect(reply, &jira::Reply::succeeded, this, [this, startAt, resultLabel](const QJsonValue &body) {
        const jira::SearchResult result = jira::SearchResult::fromJson(body.toObject());
        m_startAt = startAt;
        m_total = result.total;
        m_model->setIssues(result.issues);
        ui->issueTable->resizeColumnsToContents();
        ui->issueTable->horizontalHeader()->setSectionResizeMode(IssueTableModel::SummaryColumn,
                                                                 QHeaderView::Stretch);
        ui->issueTable->horizontalHeader()->setMinimumSectionSize(60);
        ui->issueTable->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);

        if (result.issues.isEmpty()) {
            ui->detail->clear();
            if (resultLabel)
                resultLabel->setText(tr("No matching issues"));
        } else {
            ui->issueTable->selectRow(0);
            if (resultLabel) {
                resultLabel->setText(tr("%1–%2 of %3")
                                             .arg(m_startAt + 1)
                                             .arg(m_startAt + result.issues.size())
                                             .arg(m_total));
            }
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
    ui->previousPage->setEnabled(m_startAt > 0);
    ui->nextPage->setEnabled(m_startAt + m_model->rowCount() < m_total);
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
        ui->detail->clear();
        return;
    }
    ui->detail->setIssue(m_model->issueAt(m_proxy->mapToSource(current).row()));
}

void MainWindow::refreshIssue(const QString &issueKey)
{
    jira::Reply *reply = m_client->fetchIssue(issueKey);
    connect(reply, &jira::Reply::succeeded, this, [this, issueKey](const QJsonValue &body) {
        const jira::Issue issue = jira::Issue::fromJson(body.toObject());
        if (issue.isNull())
            return;
        m_model->replaceIssue(issue);
        if (ui->detail->currentIssueKey() == issueKey)
            ui->detail->setIssue(issue);
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
    ui->timesheet->refresh();
}

void MainWindow::openIssueByKey(const QString &issueKey)
{
    // Jumping from a work log or task row back to the issue it belongs to.
    ui->tabs->setCurrentWidget(ui->searchTab);
    ui->jql->setCurrentText(QStringLiteral("key = %1").arg(issueKey));
    runSearch();
}

void MainWindow::showError(const QString &message)
{
    statusBar()->showMessage(message.section(QLatin1Char('\n'), 0, 0), 8000);
    QMessageBox::warning(this, tr("Jira"), message);
}
