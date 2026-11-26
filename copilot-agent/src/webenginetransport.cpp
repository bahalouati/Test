#include "webenginetransport.h"

#include "copilotbridge.h"

#include <QPointer>
#include <QWebEnginePage>
#include <QWebEngineScript>

void attachWebEnginePage(CopilotBridge *bridge, QWebEnginePage *page)
{
    QPointer<QWebEnginePage> p(page);
    bridge->setScriptRunner([p](const QString &script, std::function<void(const QVariant &)> done) {
        if (!p) {
            done(QVariant());
            return;
        }
        p->runJavaScript(script, QWebEngineScript::ApplicationWorld,
                         [done](const QVariant &v) { done(v); });
    });
}
