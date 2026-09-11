#pragma once

#include "core/tasktracker.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace jira { class Client; }

// The sprint task list and its stopwatch. Tracked time stays on this machine;
// the real worklog is still written by hand in Jira.
class SprintWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SprintWidget(jira::Client *client, QWidget *parent = nullptr);

    void setIdentity(const jira::User &me);
    void refresh();

signals:
    void statusMessage(const QString &message);
    void errorOccurred(const QString &message);
    void issueActivated(const QString &issueKey);

private slots:
    void rebuild();
    void updateElapsed();
    void toggleSelected();
    void markLogged();
    void removeSelected();
    void selectionChanged();

private:
    QString selectedKey() const;

    jira::Client *m_client;
    jira::TaskTracker *m_tracker;
    jira::User m_me;

    QLineEdit *m_jql;
    QPushButton *m_refresh;
    QPushButton *m_toggle;
    QPushButton *m_markLogged;
    QPushButton *m_remove;
    QLabel *m_current;
    QTableWidget *m_table;
};
