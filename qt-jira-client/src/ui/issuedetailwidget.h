#pragma once

#include "core/jiratypes.h"

#include <QList>
#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTextBrowser;

namespace jira { class Client; }

// Everything about the selected issue: fields, description, comments and the
// work log, plus the three things you actually want to do from a desktop
// client -- move the issue on, say something, and book time against it.
class IssueDetailWidget : public QWidget
{
    Q_OBJECT

public:
    explicit IssueDetailWidget(jira::Client *client, QWidget *parent = nullptr);

    void setIssue(const jira::Issue &issue);
    void clear();
    QString currentIssueKey() const { return m_issue.key; }

signals:
    // Raised after a write succeeds so the list can pick the change up.
    void issueChanged(const QString &issueKey);
    void statusMessage(const QString &message);
    void errorOccurred(const QString &message);

private slots:
    void openInBrowser();
    void applyTransition();
    void submitComment();
    void logWork();

private:
    void reloadComments();
    void reloadWorklogs();
    void reloadTransitions();
    void renderFields();
    void setBusy(bool busy);

    jira::Client *m_client;
    jira::Issue m_issue;

    QLabel *m_heading;
    QLabel *m_fields;
    QPushButton *m_openInBrowser;
    QComboBox *m_transitions;
    QPushButton *m_applyTransition;

    QTabWidget *m_tabs;
    QTextBrowser *m_description;
    QTextBrowser *m_comments;
    QPlainTextEdit *m_newComment;
    QPushButton *m_addComment;
    QTableWidget *m_worklogs;
    QLabel *m_worklogTotal;
    QPushButton *m_logWork;
};
