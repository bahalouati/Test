#pragma once

#include "core/jiratypes.h"

#include <QList>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class IssueDetailWidget; }
QT_END_NAMESPACE

namespace jira { class Client; }

// Everything about the selected issue: fields, description, comments and the
// work log, plus the three things you actually want to do from a desktop
// client -- move the issue on, say something, and book time against it.
//
// The layout lives in issuedetailwidget.ui. The constructor takes only a
// parent so the class can be promoted in Qt Designer; the client arrives
// afterwards through setClient().
class IssueDetailWidget : public QWidget
{
    Q_OBJECT

public:
    explicit IssueDetailWidget(QWidget *parent = nullptr);
    ~IssueDetailWidget() override;

    void setClient(jira::Client *client);

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

    Ui::IssueDetailWidget *ui;
    jira::Client *m_client = nullptr;
    jira::Issue m_issue;
};
