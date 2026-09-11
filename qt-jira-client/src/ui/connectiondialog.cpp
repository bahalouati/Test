#include "connectiondialog.h"
#include "ui_connectiondialog.h"

#include "core/jiraclient.h"

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>

using jira::AuthMode;
using jira::Credentials;

ConnectionDialog::ConnectionDialog(const Credentials &credentials, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ConnectionDialog)
    , m_probe(new jira::Client(this))
{
    ui->setupUi(this);

    ui->baseUrl->setText(credentials.baseUrl);

    // Populated here rather than in the .ui because each entry carries the enum
    // value as its user data.
    ui->authMode->addItem(tr("Bearer token — Jira Server / Data Center personal access token"),
                          QVariant::fromValue(int(AuthMode::Bearer)));
    ui->authMode->addItem(tr("Basic — user name or e-mail, plus password or API token"),
                          QVariant::fromValue(int(AuthMode::Basic)));
    ui->authMode->setCurrentIndex(credentials.mode == AuthMode::Bearer ? 0 : 1);

    ui->username->setText(credentials.username);
    ui->token->setText(credentials.token);
    ui->rememberToken->setChecked(credentials.rememberToken);
    ui->caCertificate->setText(credentials.caCertificatePath);
    ui->allowInvalidCertificates->setChecked(credentials.allowInvalidCertificates);

    // A reveal action beats making people paste blind into a masked field. It
    // lives inside the line edit, which the .ui format cannot express.
    QAction *reveal = ui->token->addAction(style()->standardIcon(QStyle::SP_FileDialogContentsView),
                                           QLineEdit::TrailingPosition);
    reveal->setToolTip(tr("Show the token"));
    connect(reveal, &QAction::triggered, this, [this] {
        ui->token->setEchoMode(ui->token->echoMode() == QLineEdit::Password ? QLineEdit::Normal
                                                                           : QLineEdit::Password);
    });

    m_testButton = new QPushButton(tr("Test connection"), this);
    ui->buttons->addButton(m_testButton, QDialogButtonBox::ActionRole);
    connect(m_testButton, &QPushButton::clicked, this, &ConnectionDialog::testConnection);

    connect(ui->browse, &QPushButton::clicked, this, &ConnectionDialog::browseForCertificate);
    // Qt5 overloads currentIndexChanged on int and QString; QOverload picks the
    // same one on both versions.
    connect(ui->authMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConnectionDialog::updateAuthModeHints);
    connect(ui->baseUrl, &QLineEdit::textChanged, this, &ConnectionDialog::warnAboutPlainHttp);
    updateAuthModeHints();
}

ConnectionDialog::~ConnectionDialog()
{
    delete ui;
}

Credentials ConnectionDialog::credentials() const
{
    Credentials credentials;
    credentials.baseUrl = jira::normalizeBaseUrl(ui->baseUrl->text());
    credentials.mode = AuthMode(ui->authMode->currentData().toInt());
    credentials.username = ui->username->text().trimmed();
    credentials.token = ui->token->text().trimmed();
    credentials.rememberToken = ui->rememberToken->isChecked();
    credentials.caCertificatePath = ui->caCertificate->text().trimmed();
    credentials.allowInvalidCertificates = ui->allowInvalidCertificates->isChecked();
    return credentials;
}

void ConnectionDialog::updateAuthModeHints()
{
    const bool isBasic = AuthMode(ui->authMode->currentData().toInt()) == AuthMode::Basic;

    // Server/DC bearer tokens carry their own identity, so there is nothing to
    // ask for; Basic pairs the secret with a user name.
    ui->username->setVisible(isBasic);
    ui->usernameLabel->setVisible(isBasic);

    if (isBasic) {
        ui->hint->setText(tr("Sends <code>Authorization: Basic</code>. On Jira Cloud pair your e-mail "
                             "with a token from <a href=\"https://id.atlassian.com/manage-profile/"
                             "security/api-tokens\">id.atlassian.com</a>; on a self-hosted server your "
                             "own user name and password work the same way."));
    } else {
        ui->hint->setText(tr("In Jira, open <b>Profile → Personal Access Tokens → Create token</b>. "
                             "The token authenticates on its own — no user name is needed. "
                             "Requires Jira 8.14 or newer; on an older server choose Basic above and "
                             "use your user name with your password."));
    }
    warnAboutPlainHttp();
}

void ConnectionDialog::warnAboutPlainHttp()
{
    const QString url = jira::normalizeBaseUrl(ui->baseUrl->text());
    const bool isBasic = AuthMode(ui->authMode->currentData().toInt()) == AuthMode::Basic;

    if (url.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) {
        // Basic is base64, not encryption: over plain http the user name and
        // password are readable by anything on the network path.
        setStatus(isBasic
                          ? tr("This is an http:// address, so the user name and password travel "
                               "unencrypted and anyone on the network can read them. Use https://.")
                          : tr("This is an http:// address, so the token travels unencrypted and "
                               "anyone on the network can read it. Use https://."),
                  true);
        return;
    }
    setStatus(QString(), false);
}

void ConnectionDialog::browseForCertificate()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("Select a CA certificate"),
                                                      ui->caCertificate->text(),
                                                      tr("Certificates (*.pem *.crt *.cer);;All files (*)"));
    if (!path.isEmpty())
        ui->caCertificate->setText(path);
}

void ConnectionDialog::setStatus(const QString &text, bool isError)
{
    ui->status->setText(text);
    ui->status->setStyleSheet(isError
                                      ? QStringLiteral("color: palette(bright-text); background: #8b1a1a; padding: 4px;")
                                      : QStringLiteral("color: palette(text); padding: 4px;"));
}

void ConnectionDialog::testConnection()
{
    const Credentials candidate = credentials();
    if (!candidate.isComplete()) {
        setStatus(tr("Fill in the server URL, the secret and — for Basic — the user name first."), true);
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
