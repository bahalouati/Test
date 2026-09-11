#pragma once

#include <QDateTime>
#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui { class LogWorkDialog; }
QT_END_NAMESPACE

// Collects one worklog entry: how long, when it started, and an optional note.
// The layout lives in logworkdialog.ui.
class LogWorkDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LogWorkDialog(const QString &issueKey, const QString &summary, QWidget *parent = nullptr);
    ~LogWorkDialog() override;

    QString timeSpent() const;
    QDateTime started() const;
    QString comment() const;

private slots:
    void validate();

private:
    Ui::LogWorkDialog *ui;
};
