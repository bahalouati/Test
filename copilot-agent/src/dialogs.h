#pragma once

#include "agentcontroller.h"
#include "jiraconnection.h"

#include <QDialog>
#include <QJsonArray>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QTextBrowser;

// Jira connection, the same fields as JiraDesk's connection dialog.
class JiraSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit JiraSettingsDialog(const JiraConn::Credentials &c, QWidget *parent = nullptr);
    JiraConn::Credentials credentials() const;

private:
    void updateFields();
    void test();

    QLineEdit *m_url;
    QComboBox *m_mode;
    QLineEdit *m_user;
    QLineEdit *m_token;
    QCheckBox *m_remember;
    QLineEdit *m_ca;
    QCheckBox *m_insecure;
    QLabel *m_result;
    JiraConn::Client *m_client;
};

// Copilot's plan: approve it, ask for changes, or stop.
class PlanDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PlanDialog(const QString &plan, QWidget *parent = nullptr);
    AgentController::PlanDecision decision() const { return m_decision; }
    QString note() const;

private:
    AgentController::PlanDecision m_decision = AgentController::PlanDecision::Stop;
    QPlainTextEdit *m_note;
};

// A review's findings: a sortable list, the detail of the selected one, and
// a copy of all of them as text.
class FindingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FindingsDialog(const QJsonArray &findings, const QString &target, QWidget *parent = nullptr);
    static QString asText(const QJsonArray &findings, const QString &target);

private:
    QTableWidget *m_table;
    QTextBrowser *m_detail;
    QJsonArray m_findings;
};
