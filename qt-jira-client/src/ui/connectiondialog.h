#pragma once

#include "core/credentials.h"

#include <QDialog>

class QPushButton;

QT_BEGIN_NAMESPACE
namespace Ui { class ConnectionDialog; }
QT_END_NAMESPACE

namespace jira { class Client; }

// Collects the server URL, the authentication style and the API token, and can
// prove the combination works before the dialog is accepted. The layout lives
// in connectiondialog.ui.
class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(const jira::Credentials &credentials, QWidget *parent = nullptr);
    ~ConnectionDialog() override;

    jira::Credentials credentials() const;

private slots:
    void updateAuthModeHints();
    void testConnection();
    void browseForCertificate();

private:
    void setStatus(const QString &text, bool isError);

    Ui::ConnectionDialog *ui;
    QPushButton *m_testButton;
    jira::Client *m_probe;
};
