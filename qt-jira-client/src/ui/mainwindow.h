#pragma once

#include "core/credentials.h"
#include "core/jiratypes.h"

#include <QMainWindow>

class IssueDetailWidget;
class IssueTableModel;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;

namespace jira { class Client; }

// The search-and-work window: a JQL bar over a paged result table, with the
// selected issue opened beside it.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(jira::Client *client, QWidget *parent = nullptr);

    // Called once at start-up: opens the connection dialog when nothing usable
    // has been configured yet, and otherwise runs the remembered query.
    void start();

    // Overrides the remembered query for this launch (--jql).
    void setInitialQuery(const QString &jql);

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

private:
    void setupUi();
    void loadQueryHistory();
    void rememberQuery(const QString &jql);
    void fetchPage(int startAt);
    void verifyIdentity();
    void updatePagingControls();

    jira::Client *m_client;
    IssueTableModel *m_model;
    QSortFilterProxyModel *m_proxy;
    IssueDetailWidget *m_detail;

    QComboBox *m_jql;
    QTableView *m_table;
    QLabel *m_connectionLabel;
    QLabel *m_resultLabel;
    QProgressBar *m_busy;
    QAction *m_searchAction;
    QAction *m_previousAction;
    QAction *m_nextAction;

    int m_startAt = 0;
    int m_pageSize = 50;
    int m_total = 0;
};
