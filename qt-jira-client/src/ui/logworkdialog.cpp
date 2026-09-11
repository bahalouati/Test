#include "logworkdialog.h"
#include "ui_logworkdialog.h"

#include "core/jiratypes.h"

#include <QPushButton>

LogWorkDialog::LogWorkDialog(const QString &issueKey, const QString &summary, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LogWorkDialog)
{
    ui->setupUi(this);

    setWindowTitle(tr("Log work on %1").arg(issueKey));
    ui->heading->setText(QStringLiteral("<b>%1</b> — %2").arg(issueKey, summary.toHtmlEscaped()));

    // Not expressible in the .ui: both depend on the current time.
    ui->started->setDateTime(QDateTime::currentDateTime());
    ui->started->setMaximumDateTime(QDateTime::currentDateTime().addYears(1));

    ui->buttons->button(QDialogButtonBox::Ok)->setText(tr("Log work"));

    connect(ui->timeSpent, &QLineEdit::textChanged, this, &LogWorkDialog::validate);
    validate();
}

LogWorkDialog::~LogWorkDialog()
{
    delete ui;
}

void LogWorkDialog::validate()
{
    const bool valid = jira::isValidDuration(ui->timeSpent->text());
    ui->error->setText(valid ? QString()
                             : tr("Enter a duration such as \"2h\", \"90m\" or \"1d 4h\"."));
    ui->buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

QString LogWorkDialog::timeSpent() const
{
    return ui->timeSpent->text().simplified();
}

QDateTime LogWorkDialog::started() const
{
    return ui->started->dateTime();
}

QString LogWorkDialog::comment() const
{
    return ui->comment->toPlainText().trimmed();
}
