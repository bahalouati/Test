#pragma once

#include <QDateTime>
#include <QDialog>

class QDateTimeEdit;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

// Collects one worklog entry: how long, when it started, and an optional note.
class LogWorkDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LogWorkDialog(const QString &issueKey, const QString &summary, QWidget *parent = nullptr);

    QString timeSpent() const;
    QDateTime started() const;
    QString comment() const;

private slots:
    void validate();

private:
    QLineEdit *m_timeSpent;
    QDateTimeEdit *m_started;
    QPlainTextEdit *m_comment;
    QLabel *m_error;
    QDialogButtonBox *m_buttons;
};
