#include "settingsdialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

AppSettings AppSettings::load()
{
    QSettings q;
    AppSettings s;
    s.url = q.value(QStringLiteral("url"), s.url).toString();
    s.autoLoop = q.value(QStringLiteral("autoLoop"), s.autoLoop).toBool();
    s.maxSteps = q.value(QStringLiteral("maxSteps"), s.maxSteps).toInt();
    s.charLimit = q.value(QStringLiteral("charLimit"), s.charLimit).toInt();
    s.commandTimeoutSec = q.value(QStringLiteral("commandTimeoutSec"), s.commandTimeoutSec).toInt();
    s.replyTimeoutSec = q.value(QStringLiteral("replyTimeoutSec"), s.replyTimeoutSec).toInt();
    s.selectorsJson = q.value(QStringLiteral("selectorsJson")).toString();
    return s;
}

void AppSettings::save() const
{
    QSettings q;
    q.setValue(QStringLiteral("url"), url);
    q.setValue(QStringLiteral("autoLoop"), autoLoop);
    q.setValue(QStringLiteral("maxSteps"), maxSteps);
    q.setValue(QStringLiteral("charLimit"), charLimit);
    q.setValue(QStringLiteral("commandTimeoutSec"), commandTimeoutSec);
    q.setValue(QStringLiteral("replyTimeoutSec"), replyTimeoutSec);
    q.setValue(QStringLiteral("selectorsJson"), selectorsJson);
}

QJsonObject AppSettings::selectors() const
{
    return QJsonDocument::fromJson(selectorsJson.toUtf8()).object();
}

static QSpinBox *spin(int min, int max, int value, const QString &suffix, QWidget *parent)
{
    auto *s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setValue(value);
    s->setSuffix(suffix);
    return s;
}

SettingsDialog::SettingsDialog(const AppSettings &s, QWidget *parent)
    : QDialog(parent), m_s(s)
{
    setWindowTitle(tr("Settings"));
    resize(620, 520);
    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_url = new QLineEdit(s.url, this);
    form->addRow(tr("Copilot URL:"), m_url);
    auto *hint = new QLabel(tr("The default is Microsoft 365 Copilot Chat, which applies your organisation's "
                               "enterprise data protection. Sign in with your work account."), this);
    hint->setWordWrap(true);
    form->addRow(QString(), hint);

    m_maxSteps = spin(1, 500, s.maxSteps, tr(" steps"), this);
    form->addRow(tr("Auto-loop limit:"), m_maxSteps);
    m_charLimit = spin(1000, 200000, s.charLimit, tr(" chars"), this);
    m_charLimit->setSingleStep(1000);
    form->addRow(tr("Max message size:"), m_charLimit);
    m_cmdTimeout = spin(5, 3600, s.commandTimeoutSec, tr(" s"), this);
    form->addRow(tr("Command timeout:"), m_cmdTimeout);
    m_replyTimeout = spin(30, 3600, s.replyTimeoutSec, tr(" s"), this);
    form->addRow(tr("Reply timeout:"), m_replyTimeout);
    lay->addLayout(form);

    lay->addWidget(new QLabel(tr("Selector overrides (JSON), tried before the built-in ones. "
                                 "Use this if Microsoft changes the page and the agent stops finding the input box:"), this));
    m_selectors = new QPlainTextEdit(s.selectorsJson, this);
    m_selectors->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_selectors->setPlaceholderText(QStringLiteral(
        "{\n  \"input\": [\"#my-editor\"],\n  \"send\": [\"button.send\"],\n"
        "  \"stop\": [\"button.stop\"],\n  \"messages\": [\"#chat\"],\n  \"codeBlocks\": [\"pre\"]\n}"));
    lay->addWidget(m_selectors, 1);
    m_selectorError = new QLabel(this);
    m_selectorError->setStyleSheet(QStringLiteral("color: #cf222e"));
    lay->addWidget(m_selectorError);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_selectors, &QPlainTextEdit::textChanged, this, &SettingsDialog::validate);
    validate();
}

void SettingsDialog::validate()
{
    const QString t = m_selectors->toPlainText().trimmed();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(t.toUtf8(), &err);
    const bool ok = t.isEmpty() || (err.error == QJsonParseError::NoError && doc.isObject());
    m_selectorError->setText(ok ? QString() : tr("Not a JSON object: %1").arg(err.errorString()));
    if (auto *b = findChild<QDialogButtonBox *>())
        b->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

AppSettings SettingsDialog::settings() const
{
    AppSettings s = m_s;
    s.url = m_url->text().trimmed();
    s.maxSteps = m_maxSteps->value();
    s.charLimit = m_charLimit->value();
    s.commandTimeoutSec = m_cmdTimeout->value();
    s.replyTimeoutSec = m_replyTimeout->value();
    s.selectorsJson = m_selectors->toPlainText().trimmed();
    return s;
}
