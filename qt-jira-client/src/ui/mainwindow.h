#pragma once

#include "core/credentials.h"
#include "core/jiratypes.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class IssueTableModel;
class QSortFilterProxyModel;

namespace jira { class Client; }

// The search-and-work window: a JQL bar over a paged result table with the
// selected issue beside it, plus the sprint and month tabs. The layout lives in
// mainwindow.ui, so it opens in Qt Designer.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(jira::Client *client, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Overrides the remembered query for this launch (--jql).
    void setInitialQuery(const QString &jql);

    // Called once at start-up: opens the connection dialog when nothing usable
    // has been configured yet, and otherwise runs the remembered query.
    void start();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void showConnectionDialog();
    void runSearch();
    void nextPage();
    void previousPage();
    void issueSelected(const QModelIndex &current);
    void refreshIssue(const QString &issueKey);
    void showError(const QString &message);
    void showTimesheetSettings();
    void showAbout();
    void openIssueByKey(const QString &issueKey);

private:
    void connectUi();
    void loadQueryHistory();
    void rememberQuery(const QString &jql);
    void fetchPage(int startAt);
    void verifyIdentity();
    void updatePagingControls();

    Ui::MainWindow *ui;
    jira::Client *m_client;
    IssueTableModel *m_model;
    QSortFilterProxyModel *m_proxy;

    int m_startAt = 0;
    int m_pageSize = 50;
    int m_total = 0;
};
