#include "logworkdialog.h"

#include "core/jiratypes.h"

#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QAbstractButton>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

LogWorkDialog::LogWorkDialog(const QString &issueKey, const QString &summary, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Log work on %1").arg(issueKey));
    setModal(true);

    auto *heading = new QLabel(QStringLiteral("<b>%1</b> — %2").arg(issueKey, summary.toHtmlEscaped()), this);
    heading->setWordWrap(true);

    m_timeSpent = new QLineEdit(QStringLiteral("1h"), this);
    m_timeSpent->setPlaceholderText(QStringLiteral("2h 30m"));
    m_timeSpent->setToolTip(tr("Jira duration units: w, d, h, m — for example \"1d 4h\" or \"90m\"."));

    m_started = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_started->setCalendarPopup(true);
    m_started->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_started->setMaximumDateTime(QDateTime::currentDateTime().addYears(1));

    m_comment = new QPlainTextEdit(this);
    m_comment->setPlaceholderText(tr("Optional — what the time went on"));
    m_comment->setTabChangesFocus(true);
    m_comment->setMaximumHeight(120);

    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #c0392b;"));

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Log work"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow(tr("Time spent:"), m_timeSpent);
    form->addRow(tr("Date started:"), m_started);
    form->addRow(tr("Work description:"), m_comment);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addLayout(form);
    layout->addWidget(m_error);
    layout->addWidget(m_buttons);

    connect(m_timeSpent, &QLineEdit::textChanged, this, &LogWorkDialog::validate);
    validate();
    resize(440, sizeHint().height());
}

void LogWorkDialog::validate()
{
    const bool valid = jira::isValidDuration(m_timeSpent->text());
    m_error->setText(valid ? QString()
                           : tr("Enter a duration such as \"2h\", \"90m\" or \"1d 4h\"."));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

QString LogWorkDialog::timeSpent() const
{
    return m_timeSpent->text().simplified();
}

QDateTime LogWorkDialog::started() const
{
    return m_started->dateTime();
}

QString LogWorkDialog::comment() const
{
    return m_comment->toPlainText().trimmed();
}
