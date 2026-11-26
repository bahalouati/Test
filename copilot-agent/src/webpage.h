#pragma once

#include <QWebEnginePage>

class QWebEngineProfile;

// The Copilot page. Microsoft sign-in and some links open new windows; those
// get a plain browser window on the same profile so the login cookies land
// where the chat can use them.
class WebPage : public QWebEnginePage
{
    Q_OBJECT
public:
    explicit WebPage(QWebEngineProfile *profile, QObject *parent = nullptr);

    // A persistent profile: cookies and storage survive restarts, so you sign
    // in to Microsoft 365 once.
    static QWebEngineProfile *createProfile(QObject *parent);

protected:
    QWebEnginePage *createWindow(WebWindowType type) override;
};
