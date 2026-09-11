#include "issuedetailwidget.h"

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
    const QString shown = value.trimmed().isEmpty() ? QStringLiteral("—") : value.toHtmlEscaped();
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

IssueDetailWidget::IssueDetailWidget(jira::Client *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
{
    m_heading = new QLabel(this);
    m_heading->setWordWrap(true);
    m_heading->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_openInBrowser = new QPushButton(tr("Open in Jira"), this);
    connect(m_openInBrowser, &QPushButton::clicked, this, &IssueDetailWidget::openInBrowser);

    m_transitions = new QComboBox(this);
    m_transitions->setMinimumWidth(160);
    m_transitions->setToolTip(tr("Workflow transitions available to you on this issue"));

    m_applyTransition = new QPushButton(tr("Move"), this);
    connect(m_applyTransition, &QPushButton::clicked, this, &IssueDetailWidget::applyTransition);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_transitions);
    actions->addWidget(m_applyTransition);
    actions->addStretch();
    actions->addWidget(m_openInBrowser);

    m_fields = new QLabel(this);
    m_fields->setTextFormat(Qt::RichText);
    m_fields->setWordWrap(true);
    m_fields->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_description = new QTextBrowser(this);
    m_description->setOpenExternalLinks(true);

    m_comments = new QTextBrowser(this);
    m_comments->setOpenExternalLinks(true);
    m_newComment = new QPlainTextEdit(this);
    m_newComment->setPlaceholderText(tr("Write a comment…"));
    m_newComment->setMaximumHeight(90);
    m_newComment->setTabChangesFocus(true);
    m_addComment = new QPushButton(tr("Add comment"), this);
    connect(m_addComment, &QPushButton::clicked, this, &IssueDetailWidget::submitComment);

    auto *commentActions = new QHBoxLayout;
    commentActions->addStretch();
    commentActions->addWidget(m_addComment);

    auto *commentsPage = new QWidget(this);
    auto *commentsLayout = new QVBoxLayout(commentsPage);
    commentsLayout->setContentsMargins(0, 0, 0, 0);
    commentsLayout->addWidget(m_comments, 1);
    commentsLayout->addWidget(m_newComment);
    commentsLayout->addLayout(commentActions);

    m_worklogs = new QTableWidget(0, 4, this);
    m_worklogs->setHorizontalHeaderLabels({tr("Started"), tr("Author"), tr("Time"), tr("Description")});
    m_worklogs->horizontalHeader()->setStretchLastSection(true);
    m_worklogs->verticalHeader()->setVisible(false);
    m_worklogs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_worklogs->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_worklogTotal = new QLabel(this);
    m_logWork = new QPushButton(tr("Log work…"), this);
    connect(m_logWork, &QPushButton::clicked, this, &IssueDetailWidget::logWork);

    auto *worklogActions = new QHBoxLayout;
    worklogActions->addWidget(m_worklogTotal);
    worklogActions->addStretch();
    worklogActions->addWidget(m_logWork);

    auto *worklogPage = new QWidget(this);
    auto *worklogLayout = new QVBoxLayout(worklogPage);
    worklogLayout->setContentsMargins(0, 0, 0, 0);
    worklogLayout->addWidget(m_worklogs, 1);
    worklogLayout->addLayout(worklogActions);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(m_description, tr("Description"));
    m_tabs->addTab(commentsPage, tr("Comments"));
    m_tabs->addTab(worklogPage, tr("Work log"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_heading);
    layout->addLayout(actions);
    layout->addWidget(m_fields);
    layout->addWidget(m_tabs, 1);

    clear();
}

void IssueDetailWidget::clear()
{
    m_issue = {};
    m_heading->setText(tr("<i>Select an issue.</i>"));
    m_fields->clear();
    m_description->clear();
    m_comments->clear();
    m_newComment->clear();
    m_worklogs->setRowCount(0);
    m_worklogTotal->clear();
    m_transitions->clear();
    setBusy(false);
}

void IssueDetailWidget::setBusy(bool busy)
{
    const bool hasIssue = !m_issue.isNull();
    const bool enabled = hasIssue && !busy;
    m_openInBrowser->setEnabled(hasIssue);
    m_applyTransition->setEnabled(enabled && m_transitions->count() > 0);
    m_transitions->setEnabled(enabled);
    m_addComment->setEnabled(enabled);
    m_logWork->setEnabled(enabled);
    m_newComment->setEnabled(hasIssue);
}

void IssueDetailWidget::setIssue(const jira::Issue &issue)
{
    m_issue = issue;
    if (issue.isNull()) {
        clear();
        return;
    }

    m_heading->setText(QStringLiteral("<h3 style='margin-bottom:2px;'>%1 — %2</h3>")
                               .arg(issue.key.toHtmlEscaped(), issue.summary.toHtmlEscaped()));
    renderFields();

    // v2 descriptions are wiki markup, not HTML; showing them as plain text is
    // honest, where feeding them to a rich-text view would mangle the markup.
    m_description->setPlainText(issue.description.isEmpty() ? tr("No description.") : issue.description);

    m_comments->setHtml(tr("<i>Loading comments…</i>"));
    m_worklogs->setRowCount(0);
    m_worklogTotal->clear();
    m_transitions->clear();
    m_newComment->clear();

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
    m_fields->setText(table);
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
            m_comments->setHtml(tr("<i>No comments.</i>"));
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
        m_comments->setHtml(html);
    });
    connect(reply, &jira::Reply::failed, this, [this, key](const jira::Error &error) {
        if (m_issue.key == key)
            m_comments->setHtml(tr("<i>Could not load comments: %1</i>").arg(error.message.toHtmlEscaped()));
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
        m_worklogs->setRowCount(worklogs.size());
        int totalSeconds = 0;
        for (int row = 0; row < worklogs.size(); ++row) {
            const jira::Worklog &worklog = worklogs.at(row);
            totalSeconds += worklog.timeSpentSeconds;
            m_worklogs->setItem(row, 0, new QTableWidgetItem(formatTimestamp(worklog.started)));
            m_worklogs->setItem(row, 1, new QTableWidgetItem(worklog.author.label()));
            m_worklogs->setItem(row, 2, new QTableWidgetItem(worklog.timeSpent));
            m_worklogs->setItem(row, 3, new QTableWidgetItem(worklog.comment));
        }
        m_worklogs->resizeColumnsToContents();
        m_worklogTotal->setText(worklogs.isEmpty()
                                        ? tr("No work logged yet.")
                                        : tr("%1 entries, %2 in total")
                                                  .arg(worklogs.size())
                                                  .arg(jira::formatDuration(totalSeconds)));
    });
    connect(reply, &jira::Reply::failed, this, [this, key](const jira::Error &error) {
        if (m_issue.key == key)
            m_worklogTotal->setText(tr("Could not load the work log: %1").arg(error.message));
    });
}

void IssueDetailWidget::reloadTransitions()
{
    const QString key = m_issue.key;
    jira::Reply *reply = m_client->fetchTransitions(key);
    connect(reply, &jira::Reply::succeeded, this, [this, key](const QJsonValue &body) {
        if (m_issue.key != key)
            return;
        m_transitions->clear();
        const QList<jira::Transition> transitions = jira::Transition::listFromJson(body.toObject());
        for (const jira::Transition &transition : transitions) {
            const QString label = transition.toStatus.isEmpty()
                    ? transition.name
                    : tr("%1 → %2").arg(transition.name, transition.toStatus);
            m_transitions->addItem(label, transition.id);
        }
        if (transitions.isEmpty())
            m_transitions->addItem(tr("No transitions available"), QString());
        setBusy(false);
    });
    connect(reply, &jira::Reply::failed, this, [this, key](const jira::Error &) {
        if (m_issue.key == key) {
            m_transitions->clear();
            m_transitions->addItem(tr("No transitions available"), QString());
            setBusy(false);
        }
    });
}

void IssueDetailWidget::applyTransition()
{
    const QString transitionId = m_transitions->currentData().toString();
    if (transitionId.isEmpty() || m_issue.isNull())
        return;

    const QString key = m_issue.key;
    const QString label = m_transitions->currentText();
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
    const QString body = m_newComment->toPlainText().trimmed();
    if (body.isEmpty() || m_issue.isNull())
        return;

    const QString key = m_issue.key;
    setBusy(true);
    jira::Reply *reply = m_client->addComment(key, body);
    connect(reply, &jira::Reply::succeeded, this, [this, key] {
        if (m_issue.key == key) {
            m_newComment->clear();
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
