#include "approvaldialog.h"

#include "linediff.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSyntaxHighlighter>
#include <QVBoxLayout>

namespace {

class DiffHighlighter : public QSyntaxHighlighter
{
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString &text) override
    {
        QTextCharFormat f;
        if (text.startsWith(QLatin1String("+++")) || text.startsWith(QLatin1String("---"))) {
            f.setFontWeight(QFont::Bold);
        } else if (text.startsWith(QLatin1Char('+'))) {
            f.setForeground(QColor(0x1a, 0x7f, 0x37));
            f.setBackground(QColor(0x1a, 0x7f, 0x37, 30));
        } else if (text.startsWith(QLatin1Char('-'))) {
            f.setForeground(QColor(0xcf, 0x22, 0x2e));
            f.setBackground(QColor(0xcf, 0x22, 0x2e, 30));
        } else if (text.startsWith(QLatin1String("@@"))) {
            f.setForeground(QColor(0x82, 0x50, 0xdf));
        } else {
            return;
        }
        setFormat(0, int(text.size()), f);
    }
};

} // namespace

ApprovalDialog::ApprovalDialog(const AgentAction &a, const QString &preview,
                               const QString &workspaceRoot, QWidget *parent)
    : QDialog(parent)
{
    setModal(true);
    resize(860, a.type == AgentAction::RunCommand ? 300 : 640);
    auto *lay = new QVBoxLayout(this);

    auto *title = new QLabel(this);
    title->setTextFormat(Qt::PlainText);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.2);
    tf.setBold(true);
    title->setFont(tf);
    lay->addWidget(title);

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    if (a.type == AgentAction::RunCommand) {
        setWindowTitle(tr("Run this command?"));
        title->setText(tr("Copilot wants to run a command"));
        auto *form = new QFormLayout;
        m_command = new QLineEdit(a.command, this);
        m_command->setFont(mono);
        form->addRow(tr("Command:"), m_command);
        auto *cwd = new QLabel(QDir::toNativeSeparators(
            QDir::cleanPath(workspaceRoot + QLatin1Char('/') + (a.cwd.isEmpty() ? QStringLiteral(".") : a.cwd))), this);
        cwd->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("In folder:"), cwd);
        if (a.timeoutSec > 0)
            form->addRow(tr("Timeout:"), new QLabel(tr("%1 s").arg(a.timeoutSec), this));
        lay->addLayout(form);
        auto *warn = new QLabel(tr("The command runs with your user's permissions. You can edit it before running."), this);
        warn->setWordWrap(true);
        lay->addWidget(warn);
        lay->addStretch();
    } else {
        const bool replace = a.type == AgentAction::ReplaceInFile;
        setWindowTitle(replace ? tr("Edit this file?") : tr("Write this file?"));
        const LineDiff::Stats st = LineDiff::stats(preview);
        title->setText(tr("Copilot wants to %1 %2   (+%3 / -%4)")
                           .arg(replace ? tr("edit") : tr("write"), a.path)
                           .arg(st.added).arg(st.removed));
        auto *view = new QPlainTextEdit(this);
        view->setReadOnly(true);
        view->setLineWrapMode(QPlainTextEdit::NoWrap);
        view->setFont(mono);
        view->setPlainText(preview);
        new DiffHighlighter(view->document());
        lay->addWidget(view, 1);
    }

    m_note = new QLineEdit(this);
    m_note->setPlaceholderText(tr("Optional: why you reject it, or what to do instead (sent to Copilot)"));
    lay->addWidget(m_note);

    auto *buttons = new QDialogButtonBox(this);
    auto *approve = buttons->addButton(
        a.type == AgentAction::RunCommand ? tr("Run") : tr("Apply"), QDialogButtonBox::AcceptRole);
    auto *rejectBtn = buttons->addButton(tr("Reject"), QDialogButtonBox::RejectRole);
    auto *stop = buttons->addButton(tr("Stop agent"), QDialogButtonBox::DestructiveRole);
    approve->setDefault(true);
    lay->addWidget(buttons);

    connect(approve, &QPushButton::clicked, this, [this] { m_outcome = Approved; accept(); });
    connect(rejectBtn, &QPushButton::clicked, this, [this] { m_outcome = Rejected; reject(); });
    connect(stop, &QPushButton::clicked, this, [this] { m_outcome = StopAgent; reject(); });
    // Typing a reason means you are rejecting; make that the Enter action.
    connect(m_note, &QLineEdit::textChanged, this, [approve, rejectBtn](const QString &t) {
        approve->setDefault(t.isEmpty());
        rejectBtn->setDefault(!t.isEmpty());
    });
}

QString ApprovalDialog::note() const
{
    return m_note->text();
}

QString ApprovalDialog::editedCommand() const
{
    return m_command ? m_command->text().trimmed() : QString();
}
