#include "ui/JiraImportDialog.h"

#include "ui_JiraImportDialog.h"

#include "core/Duration.h"

#include <QMessageBox>
#include <QScrollBar>

JiraImportDialog::JiraImportDialog(Settings &settings, const QDate &suggestedMonth, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::JiraImportDialog)
    , m_settings(settings)
{
    m_ui->setupUi(this);

    m_ui->lineServer->setText(settings.jiraBaseUrl());
    m_ui->lineUser->setText(settings.jiraUsername());
    m_ui->lineToken->setText(settings.jiraToken());
    m_ui->checkRememberCredentials->setChecked(!settings.jiraToken().isEmpty());

    setMonth(suggestedMonth.isValid() ? suggestedMonth : QDate::currentDate());

    connect(m_ui->buttonImport, &QPushButton::clicked, this, &JiraImportDialog::onImportClicked);
    connect(m_ui->buttonCancelImport, &QPushButton::clicked,
            this, &JiraImportDialog::onCancelClicked);
    connect(m_ui->buttonThisMonth, &QPushButton::clicked,
            this, &JiraImportDialog::onThisMonthClicked);
    connect(m_ui->buttonLastMonth, &QPushButton::clicked,
            this, &JiraImportDialog::onLastMonthClicked);
    connect(m_ui->buttonClose, &QPushButton::clicked, this, &QDialog::reject);
    connect(&m_client, &JiraClient::progressChanged,
            this, &JiraImportDialog::onProgressChanged);

    appendLog(QStringLiteral("Ready. Choose a period and press Import."));
}

JiraImportDialog::~JiraImportDialog()
{
    delete m_ui;
}

QVector<WorkEntry> JiraImportDialog::importedEntries() const
{
    return m_importedEntries;
}

void JiraImportDialog::setMonth(const QDate &anyDayOfMonth)
{
    const QDate first(anyDayOfMonth.year(), anyDayOfMonth.month(), 1);
    m_ui->dateFrom->setDate(first);
    m_ui->dateTo->setDate(first.addMonths(1).addDays(-1));
}

void JiraImportDialog::onThisMonthClicked()
{
    setMonth(QDate::currentDate());
}

void JiraImportDialog::onLastMonthClicked()
{
    setMonth(QDate::currentDate().addMonths(-1));
}

void JiraImportDialog::appendLog(const QString &line)
{
    m_ui->textLog->appendPlainText(line);
    QScrollBar *scrollBar = m_ui->textLog->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void JiraImportDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_ui->buttonImport->setEnabled(!busy);
    m_ui->buttonCancelImport->setEnabled(busy);
    m_ui->buttonClose->setEnabled(!busy);
    m_ui->lineServer->setEnabled(!busy);
    m_ui->lineUser->setEnabled(!busy);
    m_ui->lineToken->setEnabled(!busy);
    m_ui->dateFrom->setEnabled(!busy);
    m_ui->dateTo->setEnabled(!busy);
}

void JiraImportDialog::onProgressChanged(int done, int total, const QString &message)
{
    if (total > 0) {
        m_ui->progressBar->setMaximum(total);
        m_ui->progressBar->setValue(done);
    } else {
        // An unknown total shows as a busy indicator rather than a stuck bar.
        m_ui->progressBar->setMaximum(0);
    }
    m_ui->progressBar->setFormat(QStringLiteral("%1 (%p%)").arg(message));
}

void JiraImportDialog::onCancelClicked()
{
    m_client.cancel();
    appendLog(QStringLiteral("Stopping after the current issue..."));
}

void JiraImportDialog::onImportClicked()
{
    JiraConfig config;
    config.baseUrl = m_ui->lineServer->text().trimmed();
    config.username = m_ui->lineUser->text().trimmed();
    config.token = m_ui->lineToken->text();
    config.sprintField = m_settings.jiraSprintField();
    config.testsheetKeyword = m_settings.jiraTestsheetKeyword();
    config.mergeRequestMarker = m_settings.jiraMergeRequestMarker();

    QString reason;
    if (!config.isUsable(&reason)) {
        QMessageBox::warning(this, QStringLiteral("Import from Jira"), reason);
        return;
    }

    // The server and user name are always worth keeping; the token only when
    // the user asked for it.
    m_settings.setJiraBaseUrl(config.baseUrl);
    m_settings.setJiraUsername(config.username);
    m_settings.setJiraToken(m_ui->checkRememberCredentials->isChecked() ? config.token
                                                                       : QString());
    m_settings.sync();

    m_client.setConfig(config);
    m_importedEntries.clear();

    setBusy(true);
    appendLog(QString());
    appendLog(QStringLiteral("Importing %1 to %2 ...")
                  .arg(m_ui->dateFrom->date().toString(Qt::ISODate),
                       m_ui->dateTo->date().toString(Qt::ISODate)));

    JiraImportResult result;
    QString error;
    const bool ok = m_client.fetchWorklogs(m_ui->dateFrom->date(), m_ui->dateTo->date(),
                                           &result, &error);

    for (const QString &message : result.messages)
        appendLog(message);

    setBusy(false);
    m_ui->progressBar->setMaximum(100);

    if (!ok) {
        m_ui->progressBar->setValue(0);
        appendLog(QStringLiteral("Import failed: %1").arg(error));
        QMessageBox::critical(this, QStringLiteral("Import from Jira"), error);
        return;
    }

    m_importedEntries = result.entries;
    m_ui->progressBar->setValue(m_ui->progressBar->maximum());

    int totalMinutes = 0;
    for (const WorkEntry &entry : m_importedEntries)
        totalMinutes += entry.minutes;

    appendLog(QStringLiteral("Found %1 worklog(s) across %2 issue(s), %3 in total.")
                  .arg(result.worklogsMatched)
                  .arg(result.issuesScanned)
                  .arg(Duration::format(totalMinutes)));

    if (m_importedEntries.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Import from Jira"),
                                 QStringLiteral("No worklogs of yours were found in that period."));
        return;
    }

    accept();
}
