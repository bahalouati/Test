#pragma once

#include <QString>
#include <QVector>

// Which Copilot the app talks to. Work and personal Copilot are different
// services with different accounts and different data protection, so the
// app keeps them apart: their own Edge profile each, and a tab of one is
// never taken for the other.
namespace CopilotService {

enum Kind {
    Work,       // Microsoft 365 Copilot Chat: work account, enterprise data protection
    Personal,   // Copilot at copilot.com: personal Microsoft account, consumer terms
    Custom      // any other address the user typed
};

QString defaultUrl(Kind k);              // empty for Custom
Kind kindOfUrl(const QString &url);      // Custom when the host is neither
Kind kindOfHost(const QString &host);

// Whether a tab on `host` is the same Copilot service as `startHost`
// (Copilot moves between addresses, e.g. m365.cloud.microsoft redirects to
// copilot.cloud.microsoft), never across work and personal.
bool sameService(const QString &host, const QString &startHost);

// The sign-in pages that service passes through.
bool isSignInHost(const QString &host, Kind k);

// Edge profile folder name for the service, so work and personal accounts
// never share cookies or sign-ins.
QString profileFolder(Kind k);

} // namespace CopilotService
