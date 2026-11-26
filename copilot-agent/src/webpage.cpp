#include "webpage.h"

#include <QRegularExpression>
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWidget>

WebPage::WebPage(QWebEngineProfile *profile, QObject *parent)
    : QWebEnginePage(profile, parent)
{
}

QWebEngineProfile *WebPage::createProfile(QObject *parent)
{
    auto *profile = new QWebEngineProfile(QStringLiteral("copilot-agent"), parent);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    // Some Microsoft pages refuse browsers they do not recognise; the stock
    // agent string names QtWebEngine, so drop that token and look like the
    // Chromium it is.
    QString ua = profile->httpUserAgent();
    ua.remove(QRegularExpression(QStringLiteral("QtWebEngine/\\S+\\s*")));
    profile->setHttpUserAgent(ua);
    return profile;
}

QWebEnginePage *WebPage::createWindow(WebWindowType type)
{
    Q_UNUSED(type);
    auto *win = new QWidget(nullptr, Qt::Window);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->resize(900, 700);
    auto *lay = new QVBoxLayout(win);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *view = new QWebEngineView(win);
    auto *page = new WebPage(profile(), view);
    view->setPage(page);
    lay->addWidget(view);
    connect(page, &QWebEnginePage::windowCloseRequested, win, &QWidget::close);
    connect(page, &QWebEnginePage::titleChanged, win, &QWidget::setWindowTitle);
    win->show();
    return page;
}
