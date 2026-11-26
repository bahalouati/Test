#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
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

// Rough Chromium generation of the WebEngine we were built with, for the hint.
#if QT_VERSION < QT_VERSION_CHECK(5, 13, 0)
#  define QTWEBENGINECORE_CHROMIUM_VERSION_HINT "69"
#elif QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
#  define QTWEBENGINECORE_CHROMIUM_VERSION_HINT "73-77"
#else
#  define QTWEBENGINECORE_CHROMIUM_VERSION_HINT "80-87"
#endif

AppSettings AppSettings::load()
{
    QSettings q;
    AppSettings s;
    s.url = q.value(QStringLiteral("url"), s.url).toString();
    s.mode = q.value(QStringLiteral("mode"), s.mode).toString();
    if (s.mode != QLatin1String("edge") && s.mode != QLatin1String("clipboard") && s.mode != QLatin1String("builtin"))
        s.mode = QStringLiteral("edge");
    s.browserPath = q.value(QStringLiteral("browserPath")).toString();
    s.autoLoop = q.value(QStringLiteral("autoLoop"), s.autoLoop).toBool();
    s.maxSteps = q.value(QStringLiteral("maxSteps"), s.maxSteps).toInt();
    s.charLimit = q.value(QStringLiteral("charLimit"), s.charLimit).toInt();
    s.commandTimeoutSec = q.value(QStringLiteral("commandTimeoutSec"), s.commandTimeoutSec).toInt();
    s.replyTimeoutSec = q.value(QStringLiteral("replyTimeoutSec"), s.replyTimeoutSec).toInt();
    s.selectorsJson = q.value(QStringLiteral("selectorsJson")).toString();
    s.autoSendDelaySec = q.value(QStringLiteral("autoSendDelaySec"), s.autoSendDelaySec).toInt();
    return s;
}

void AppSettings::save() const
{
    QSettings q;
    q.setValue(QStringLiteral("url"), url);
    q.setValue(QStringLiteral("mode"), mode);
    q.setValue(QStringLiteral("browserPath"), browserPath);
    q.setValue(QStringLiteral("autoLoop"), autoLoop);
    q.setValue(QStringLiteral("maxSteps"), maxSteps);
    q.setValue(QStringLiteral("charLimit"), charLimit);
    q.setValue(QStringLiteral("commandTimeoutSec"), commandTimeoutSec);
    q.setValue(QStringLiteral("replyTimeoutSec"), replyTimeoutSec);
    q.setValue(QStringLiteral("selectorsJson"), selectorsJson);
    q.setValue(QStringLiteral("autoSendDelaySec"), autoSendDelaySec);
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

    m_mode = new QComboBox(this);
    m_mode->addItem(tr("Microsoft Edge, automatic (recommended)"), QStringLiteral("edge"));
    m_mode->addItem(tr("My own browser, copy & paste"), QStringLiteral("clipboard"));
    m_mode->addItem(tr("Built-in browser (Chromium %1)").arg(QStringLiteral(QTWEBENGINECORE_CHROMIUM_VERSION_HINT)),
                    QStringLiteral("builtin"));
    m_mode->setCurrentIndex(qMax(0, m_mode->findData(s.mode)));
    form->addRow(tr("Copilot runs in:"), m_mode);
    auto *modeHint = new QLabel(tr("Edge: the app opens Copilot in its own Edge window and types, sends and "
                                   "reads replies there. Sign in once in that window. The built-in browser "
                                   "is too old for Microsoft 365 with Qt 5.12."), this);
    modeHint->setWordWrap(true);
    form->addRow(QString(), modeHint);
    m_browserPath = new QLineEdit(s.browserPath, this);
    m_browserPath->setPlaceholderText(tr("Automatic: Edge, else Chrome"));
    form->addRow(tr("Browser program:"), m_browserPath);

    m_maxSteps = spin(1, 500, s.maxSteps, tr(" steps"), this);
    form->addRow(tr("Auto-loop limit:"), m_maxSteps);
    m_autoDelay = spin(0, 300, s.autoSendDelaySec, tr(" s"), this);
    m_autoDelay->setToolTip(tr("How long to wait after Copilot's reply before sending the results "
                               "automatically. A human pace avoids rate limits."));
    form->addRow(tr("Pause before auto-send:"), m_autoDelay);
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
    s.mode = m_mode->currentData().toString();
    s.browserPath = m_browserPath->text().trimmed();
    s.maxSteps = m_maxSteps->value();
    s.charLimit = m_charLimit->value();
    s.commandTimeoutSec = m_cmdTimeout->value();
    s.replyTimeoutSec = m_replyTimeout->value();
    s.autoSendDelaySec = m_autoDelay->value();
    s.selectorsJson = m_selectors->toPlainText().trimmed();
    return s;
}
