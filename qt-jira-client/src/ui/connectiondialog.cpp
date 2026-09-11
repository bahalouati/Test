#include "connectiondialog.h"

#include "core/jiraclient.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

using jira::AuthMode;
using jira::Credentials;

ConnectionDialog::ConnectionDialog(const Credentials &credentials, QWidget *parent)
    : QDialog(parent)
    , m_probe(new jira::Client(this))
{
    setWindowTitle(tr("Connect to Jira"));
    setModal(true);

    m_baseUrl = new QLineEdit(credentials.baseUrl, this);
    m_baseUrl->setPlaceholderText(QStringLiteral("https://jira.example.com"));
    m_baseUrl->setToolTip(tr("The address you open Jira with. If Jira is served under a path, "
                             "include it: https://intranet.example.com/jira"));

    m_authMode = new QComboBox(this);
    m_authMode->addItem(tr("Bearer token — Jira Server / Data Center personal access token"),
                        QVariant::fromValue(int(AuthMode::Bearer)));
    m_authMode->addItem(tr("Basic — user name or e-mail, plus password or API token"),
                        QVariant::fromValue(int(AuthMode::Basic)));
    m_authMode->setCurrentIndex(credentials.mode == AuthMode::Bearer ? 0 : 1);

    m_username = new QLineEdit(credentials.username, this);
    m_username->setPlaceholderText(tr("user name, or you@example.com on Cloud"));
    m_usernameLabel = new QLabel(tr("User:"), this);

    m_token = new QLineEdit(credentials.token, this);
    m_token->setEchoMode(QLineEdit::Password);
    m_token->setPlaceholderText(tr("API token"));
    // A reveal action beats making people paste blind into a masked field.
    QAction *reveal = m_token->addAction(style()->standardIcon(QStyle::SP_FileDialogContentsView),
                                         QLineEdit::TrailingPosition);
    reveal->setToolTip(tr("Show the token"));
    connect(reveal, &QAction::triggered, this, [this] {
        m_token->setEchoMode(m_token->echoMode() == QLineEdit::Password ? QLineEdit::Normal
                                                                       : QLineEdit::Password);
    });

    m_rememberToken = new QCheckBox(tr("Remember the token on this computer"), this);
    m_rememberToken->setChecked(credentials.rememberToken);
    m_rememberToken->setToolTip(tr("Stored in this application's settings file as plain text. "
                                   "Leave it off and set JIRA_API_TOKEN in the environment instead "
                                   "if the machine is shared."));

    m_caCertificate = new QLineEdit(credentials.caCertificatePath, this);
    m_caCertificate->setPlaceholderText(tr("optional — PEM file for an internal certificate authority"));
    auto *browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &ConnectionDialog::browseForCertificate);
    auto *certificateRow = new QHBoxLayout;
    certificateRow->setContentsMargins(0, 0, 0, 0);
    certificateRow->addWidget(m_caCertificate);
    certificateRow->addWidget(browse);

    m_allowInvalidCertificates = new QCheckBox(tr("Accept any certificate (insecure)"), this);
    m_allowInvalidCertificates->setChecked(credentials.allowInvalidCertificates);
    m_allowInvalidCertificates->setToolTip(tr("Disables certificate verification entirely. Prefer pointing "
                                              "the field above at your certificate authority's PEM file."));

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setTextFormat(Qt::RichText);
    m_hint->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_hint->setOpenExternalLinks(true);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Server URL:"), m_baseUrl);
    form->addRow(tr("Jira type:"), m_authMode);
    form->addRow(m_usernameLabel, m_username);
    form->addRow(tr("API token:"), m_token);
    form->addRow(QString(), m_rememberToken);
    form->addRow(tr("CA certificate:"), certificateRow);
    form->addRow(QString(), m_allowInvalidCertificates);

    m_testButton = new QPushButton(tr("Test connection"), this);
    connect(m_testButton, &QPushButton::clicked, this, &ConnectionDialog::testConnection);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_buttons->addButton(m_testButton, QDialogButtonBox::ActionRole);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_hint);
    layout->addWidget(m_status);
    layout->addStretch();
    layout->addWidget(m_buttons);

    connect(m_authMode, &QComboBox::currentIndexChanged, this, &ConnectionDialog::updateAuthModeHints);
    updateAuthModeHints();
    resize(560, sizeHint().height());
}

Credentials ConnectionDialog::credentials() const
{
    Credentials credentials;
    credentials.baseUrl = jira::normalizeBaseUrl(m_baseUrl->text());
    credentials.mode = AuthMode(m_authMode->currentData().toInt());
    credentials.username = m_username->text().trimmed();
    credentials.token = m_token->text().trimmed();
    credentials.rememberToken = m_rememberToken->isChecked();
    credentials.caCertificatePath = m_caCertificate->text().trimmed();
    credentials.allowInvalidCertificates = m_allowInvalidCertificates->isChecked();
    return credentials;
}

void ConnectionDialog::updateAuthModeHints()
{
    const bool isCloud = AuthMode(m_authMode->currentData().toInt()) == AuthMode::Basic;

    // Server/DC bearer tokens carry their own identity, so there is nothing to
    // ask for; Cloud pairs the token with the account e-mail.
    m_username->setVisible(isCloud);
    m_usernameLabel->setVisible(isCloud);

    if (isCloud) {
        m_hint->setText(tr("Sends <code>Authorization: Basic</code>. On Jira Cloud pair your e-mail with a token "
                           "from <a href=\"https://id.atlassian.com/manage-profile/security/api-tokens\">"
                           "id.atlassian.com</a>; on a self-hosted server your own user name and password work "
                           "the same way."));
    } else {
        m_hint->setText(tr("In Jira, open <b>Profile → Personal Access Tokens → Create token</b>. "
                           "The token authenticates on its own — no user name is needed. "
                           "Requires Jira 8.14 or newer; on an older server choose Basic above and "
                           "use your user name with your password."));
    }
    setStatus(QString(), false);
}

void ConnectionDialog::browseForCertificate()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("Select a CA certificate"),
                                                      m_caCertificate->text(),
                                                      tr("Certificates (*.pem *.crt *.cer);;All files (*)"));
    if (!path.isEmpty())
        m_caCertificate->setText(path);
}

void ConnectionDialog::setStatus(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setStyleSheet(isError ? QStringLiteral("color: palette(bright-text); background: #8b1a1a; padding: 4px;")
                                    : QStringLiteral("color: palette(text); padding: 4px;"));
}

void ConnectionDialog::testConnection()
{
    const Credentials candidate = credentials();
    if (!candidate.isComplete()) {
        setStatus(tr("Fill in the server URL, the token and — for Jira Cloud — the e-mail address first."), true);
        return;
    }

    m_probe->setCredentials(candidate);
    m_testButton->setEnabled(false);
    setStatus(tr("Contacting %1…").arg(candidate.baseUrl), false);

    jira::Reply *reply = m_probe->fetchMyself();
    connect(reply, &jira::Reply::succeeded, this, [this](const QJsonValue &body) {
        m_testButton->setEnabled(true);
        const jira::User me = jira::User::fromJson(body.toObject());
        setStatus(tr("Connected as %1.").arg(me.label()), false);
    });
    connect(reply, &jira::Reply::failed, this, [this](const jira::Error &error) {
        m_testButton->setEnabled(true);
        setStatus(error.toString(), true);
    });
}
