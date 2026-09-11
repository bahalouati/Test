#pragma once

#include "core/tasktracker.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class SprintWidget; }
QT_END_NAMESPACE

namespace jira { class Client; }

// The sprint task list and its stopwatch. Tracked time stays on this machine;
// the real worklog is still written by hand in Jira.
//
// The layout lives in sprintwidget.ui. The constructor takes only a parent so
// the class can be promoted in Qt Designer; the client arrives afterwards
// through setClient().
class SprintWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SprintWidget(QWidget *parent = nullptr);
    ~SprintWidget() override;

    void setClient(jira::Client *client);
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

    Ui::SprintWidget *ui;
    jira::Client *m_client = nullptr;
    jira::TaskTracker *m_tracker;
    jira::User m_me;
};
