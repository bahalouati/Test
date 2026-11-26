#pragma once

#include "agentaction.h"

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;

// Asks before any write or command. Shows a diff for file changes and the
// exact command line (editable) for commands.
class ApprovalDialog : public QDialog
{
    Q_OBJECT
public:
    enum Outcome { Approved, Rejected, StopAgent };

    // preview: unified diff for file changes; ignored for commands.
    ApprovalDialog(const AgentAction &action, const QString &preview,
                   const QString &workspaceRoot, QWidget *parent = nullptr);

    Outcome outcome() const { return m_outcome; }
    QString note() const;
    QString editedCommand() const;

private:
    Outcome m_outcome = Rejected;
    QLineEdit *m_command = nullptr;
    QLineEdit *m_note = nullptr;
};
