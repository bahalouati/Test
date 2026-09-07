#pragma once

#include "core/credentials.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace jira { class Client; }

// Collects the server URL, the authentication style and the API token, and can
// prove the combination works before the dialog is accepted.
class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(const jira::Credentials &credentials, QWidget *parent = nullptr);

    jira::Credentials credentials() const;

private slots:
    void updateAuthModeHints();
    void testConnection();
    void browseForCertificate();

private:
    void setStatus(const QString &text, bool isError);

    QLineEdit *m_baseUrl;
    QComboBox *m_authMode;
    QLabel *m_usernameLabel;
    QLineEdit *m_username;
    QLineEdit *m_token;
    QCheckBox *m_rememberToken;
    QLineEdit *m_caCertificate;
    QCheckBox *m_allowInvalidCertificates;
    QLabel *m_hint;
    QLabel *m_status;
    QPushButton *m_testButton;
    QDialogButtonBox *m_buttons;
    jira::Client *m_probe;
};
