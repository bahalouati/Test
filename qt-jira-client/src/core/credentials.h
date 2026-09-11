#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace jira {

// Jira Cloud and Jira Server/Data Center both call the thing you paste an
// "API token", but they authenticate with it differently.
enum class AuthMode {
    Basic,   // Jira Cloud: HTTP Basic with email + API token
    Bearer   // Jira Server / Data Center: a Personal Access Token
};

QString authModeToString(AuthMode mode);
AuthMode authModeFromString(const QString &text, AuthMode fallback = AuthMode::Bearer);

// Trims, defaults the scheme to https, drops a trailing slash and forgives the
// common paste of a full REST path. A context path ("/jira") is preserved --
// self-hosted instances frequently live under one.
QString normalizeBaseUrl(const QString &raw);

struct Credentials {
    QString baseUrl;
    // Bearer by default: a self-hosted Jira at your own address is the common
    // case, and Cloud is one click away in the connection dialog.
    AuthMode mode = AuthMode::Bearer;
    QString username;   // e-mail for Basic; unused for Bearer
    QString token;
    bool rememberToken = false;

    // Self-hosted Jira often sits behind an internal certificate authority.
    QString caCertificatePath;
    bool allowInvalidCertificates = false;

    bool isComplete() const;
    QByteArray authorizationHeader() const;

    // Reads QSettings, then lets JIRA_BASE_URL / JIRA_USER / JIRA_API_TOKEN
    // override -- so a shared workstation or a script can supply the token
    // without it ever touching disk.
    static Credentials load();
    void save() const;
    static void forgetToken();
};

} // namespace jira
