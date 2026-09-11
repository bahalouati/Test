#include "issuedetailwidget.h"
#include "ui_issuedetailwidget.h"

#include "core/jiraclient.h"
#include "logworkdialog.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QString fieldRow(const QString &name, const QString &value)
{
    // From its code point, so a compiler reading this file as anything but
    // UTF-8 cannot turn it into mojibake.
    const QString shown = value.trimmed().isEmpty() ? QString(QChar(0x2014))
                                                    : value.toHtmlEscaped();
    return QStringLiteral("<tr><td style='padding-right:12px; color:gray;'>%1</td><td>%2</td></tr>")
            .arg(name.toHtmlEscaped(), shown);
}

QString formatTimestamp(const QDateTime &dateTime)
{
    if (!dateTime.isValid())
        return {};
    return QLocale().toString(dateTime.toLocalTime(), QLocale::ShortFormat);
}

} // namespace

IssueDetailWidget::IssueDetailWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::IssueDetailWidget)
{
    ui->setupUi(this);

    connect(ui->openInBrowser, &QPushButton::clicked, this, &IssueDetailWidget::openInBrowser);
    connect(ui->applyTransition, &QPushButton::clicked, this, &IssueDetailWidget::applyTransition);
    connect(ui->addComment, &QPushButton::clicked, this, &IssueDetailWidget::submitComment);
    connect(ui->logWork, &QPushButton::clicked, this, &IssueDetailWidget::logWork);

    clear();
}

IssueDetailWidget::~IssueDetailWidget()
{
    delete ui;
}

void IssueDetailWidget::setClient(jira::Client *client)
{
    m_client = client;
}

void IssueDetailWidget::clear()
{
    m_issue = {};
    ui->heading->setText(tr("<i>Select an issue.</i>"));
    ui->fields->clear();
    ui->description->clear();
    ui->comments->clear();
    ui->newComment->clear();
    ui->worklogs->setRowCount(0);
    ui->worklogTotal->clear();
    ui->transitions->clear();
    setBusy(false);
}

void IssueDetailWidget::setBusy(bool busy)
{
    const bool hasIssue = !m_issue.isNull();
    const bool enabled = hasIssue && !busy;
    ui->openInBrowser->setEnabled(hasIssue);
    ui->applyTransition->setEnabled(enabled && ui->transitions->count() > 0);
    ui->transitions->setEnabled(enabled);
    ui->addComment->setEnabled(enabled);
    ui->logWork->setEnabled(enabled);
    ui->newComment->setEnabled(hasIssue);
}

void IssueDetailWidget::setIssue(const jira::Issue &issue)
{
    if (!m_client) {
        clear();
        return;
    }
    m_issue = issue;
    if (issue.isNull()) {
        clear();
        return;
    }

    ui->heading->setText(QStringLiteral("<h3 style='margin-bottom:2px;'>%1 — %2</h3>")
                               .arg(issue.key.toHtmlEscaped(), issue.summary.toHtmlEscaped()));
    renderFields();

    // v2 descriptions are wiki markup, not HTML; showing them as plain text is
    // honest, where feeding them to a rich-text view would mangle the markup.
    ui->description->setPlainText(issue.description.isEmpty() ? tr("No description.") : issue.description);

    ui->comments->setHtml(tr("<i>Loading comments…</i>"));
    ui->worklogs->setRowCount(0);
    ui->worklogTotal->clear();
    ui->transitions->clear();
    ui->newComment->clear();

    setBusy(false);
    reloadComments();
    reloadWorklogs();
    reloadTransitions();
}

void IssueDetailWidget::renderFields()
{
    QString table = QStringLiteral("<table cellspacing='0' cellpadding='2'>");
    table += fieldRow(tr("Status"), m_issue.status);
    table += fieldRow(tr("Type"), m_issue.issueType);
    table += fieldRow(tr("Priority"), m_issue.priority);
    table += fieldRow(tr("Assignee"), m_issue.assignee.isNull() ? tr("Unassigned") : m_issue.assignee.label());
    table += fieldRow(tr("Reporter"), m_issue.reporter.label());
    table += fieldRow(tr("Project"),
                      m_issue.projectName.isEmpty()
                              ? m_issue.projectKey
                              : QStringLiteral("%1 (%2)").arg(m_issue.projectName, m_issue.projectKey));
    if (!m_issue.resolution.isEmpty())
        table += fieldRow(tr("Resolution"), m_issue.resolution);
    if (!m_issue.labels.isEmpty())
        table += fieldRow(tr("Labels"), m_issue.labels.join(QStringLiteral(", ")));
    if (!m_issue.components.isEmpty())
        table += fieldRow(tr("Components"), m_issue.components.join(QStringLiteral(", ")));
    if (m_issue.dueDate.isValid())
        table += fieldRow(tr("Due"), QLocale().toString(m_issue.dueDate, QLocale::ShortFormat));
    if (m_issue.originalEstimateSeconds > 0)
        table += fieldRow(tr("Estimate"), jira::formatDuration(m_issue.originalEstimateSeconds));
    table += fieldRow(tr("Updated"), formatTimestamp(m_issue.updated));
    table += QStringLiteral("</table>");
    ui->fields->setText(table);
}

void IssueDetailWidget::openInBrowser()
{
    const QUrl url = m_client->browseUrl(m_issue.key);
    if (url.isValid())
        QDesktopServices::openUrl(url);
}

void IssueDetailWidget::reloadComments()
{
    const QString key = m_issue.key;
    jira::Reply *reply = m_client->fetchComments(key);
    connect(reply, &jira::Reply::succeeded, this, [this, key](const QJsonValue &body) {
        if (m_issue.key != key)
            return;   // selection moved on while the request was in flight
        const QList<jira::Comment> comments = jira::Comment::listFromJson(body.toObject());
        if (comments.isEmpty()) {
            ui->comments->setHtml(tr("<i>No comments.</i>"));
            return;
        }
        QString html;
        for (const jira::Comment &comment : comments) {
            html += QStringLiteral("<p><b>%1</b> <span style='color:gray;'>%2</span><br/>%3</p><hr/>")
                            .arg(comment.author.label().toHtmlEscaped(),
                                 formatTimestamp(comment.created).toHtmlEscaped(),
                                 comment.body.toHtmlEscaped().replace(QLatin1Char('\n'),
                                                                      QLatin1String("<br/>")));
        }
        ui->comments->setHtml(html);
    });
    connect(reply, &jira::Reply::failed, this, [this, key](const jira::Error &error) {
        if (m_issue.key == key)
            ui->comments->setHtml(tr("<i>Could not load comments: %1</i>").arg(error.message.toHtmlEscaped()));
    });
}

void IssueDetailWidget::reloadWorklogs()
{
    const QString key = m_issue.key;
    jira::Reply *reply = m_client->fetchWorklogs(key);
    connect(reply, &jira::Reply::succeeded, this, [this, key](const QJsonValue &body) {
        if (m_issue.key != key)
            return;
        const QList<jira::Worklog> worklogs = jira::Worklog::listFromJson(body.toObject());
        ui->worklogs->setRowCount(worklogs.size());
        int totalSeconds = 0;
        for (int row = 0; row < worklogs.size(); ++row) {
            const jira::Worklog &worklog = worklogs.at(row);
            totalSeconds += worklog.timeSpentSeconds;
            ui->worklogs->setItem(row, 0, new QTableWidgetItem(formatTimestamp(worklog.started)));
            ui->worklogs->setItem(row, 1, new QTableWidgetItem(worklog.author.label()));
            ui->worklogs->setItem(row, 2, new QTableWidgetItem(worklog.timeSpent));
            ui->worklogs->setItem(row, 3, new QTableWidgetItem(worklog.comment));
        }
        ui->worklogs->resizeColumnsToContents();
        ui->worklogTotal->setText(worklogs.isEmpty()
                                        ? tr("No work logged yet.")
                                        : tr("%1 entries, %2 in total")
                                                  .arg(worklogs.size())
                                                  .arg(jira::formatDuration(totalSeconds)));
    });
    connect(reply, &jira::Reply::failed, this, [this, key](const jira::Error &error) {
        if (m_issue.key == key)
            ui->worklogTotal->setText(tr("Could not load the work log: %1").arg(error.message));
    });
}

void IssueDetailWidget::reloadTransitions()
{
    const QString key = m_issue.key;
    jira::Reply *reply = m_client->fetchTransitions(key);
    connect(reply, &jira::Reply::succeeded, this, [this, key](const QJsonValue &body) {
        if (m_issue.key != key)
            return;
        ui->transitions->clear();
        const QList<jira::Transition> transitions = jira::Transition::listFromJson(body.toObject());
        for (const jira::Transition &transition : transitions) {
            const QString label = transition.toStatus.isEmpty()
                    ? transition.name
                    : tr("%1 → %2").arg(transition.name, transition.toStatus);
            ui->transitions->addItem(label, transition.id);
        }
        if (transitions.isEmpty())
            ui->transitions->addItem(tr("No transitions available"), QString());
        setBusy(false);
    });
    connect(reply, &jira::Reply::failed, this, [this, key](const jira::Error &) {
        if (m_issue.key == key) {
            ui->transitions->clear();
            ui->transitions->addItem(tr("No transitions available"), QString());
            setBusy(false);
        }
    });
}

void IssueDetailWidget::applyTransition()
{
    const QString transitionId = ui->transitions->currentData().toString();
    if (transitionId.isEmpty() || m_issue.isNull())
        return;

    const QString key = m_issue.key;
    const QString label = ui->transitions->currentText();
    setBusy(true);

    jira::Reply *reply = m_client->applyTransition(key, transitionId);
    connect(reply, &jira::Reply::succeeded, this, [this, key, label] {
        emit statusMessage(tr("%1: %2").arg(key, label));
        emit issueChanged(key);
        setBusy(false);
    });
    connect(reply, &jira::Reply::failed, this, [this, key](const jira::Error &error) {
        // A transition with required screen fields fails here; the message from
        // Jira names the field, so pass it through unedited.
        emit errorOccurred(tr("Could not move %1: %2").arg(key, error.toString()));
        setBusy(false);
    });
}

void IssueDetailWidget::submitComment()
{
    const QString body = ui->newComment->toPlainText().trimmed();
    if (body.isEmpty() || m_issue.isNull())
        return;

    const QString key = m_issue.key;
    setBusy(true);
    jira::Reply *reply = m_client->addComment(key, body);
    connect(reply, &jira::Reply::succeeded, this, [this, key] {
        if (m_issue.key == key) {
            ui->newComment->clear();
            reloadComments();
        }
        emit statusMessage(tr("Comment added to %1.").arg(key));
        setBusy(false);
    });
    connect(reply, &jira::Reply::failed, this, [this, key](const jira::Error &error) {
        emit errorOccurred(tr("Could not comment on %1: %2").arg(key, error.toString()));
        setBusy(false);
    });
}

void IssueDetailWidget::logWork()
{
    if (m_issue.isNull())
        return;

    LogWorkDialog dialog(m_issue.key, m_issue.summary, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString key = m_issue.key;
    setBusy(true);
    jira::Reply *reply = m_client->addWorklog(key, dialog.timeSpent(), dialog.started(), dialog.comment());
    connect(reply, &jira::Reply::succeeded, this, [this, key, spent = dialog.timeSpent()] {
        if (m_issue.key == key)
            reloadWorklogs();
        emit statusMessage(tr("Logged %1 on %2.").arg(spent, key));
        emit issueChanged(key);
        setBusy(false);
    });
    connect(reply, &jira::Reply::failed, this, [this, key](const jira::Error &error) {
        emit errorOccurred(tr("Could not log work on %1: %2").arg(key, error.toString()));
        setBusy(false);
    });
}
