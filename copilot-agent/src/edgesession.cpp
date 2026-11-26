#include "edgesession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <memory>

namespace {

const int kHttpTimeoutMs = 3000;
const int kLaunchWaitSteps = 60;      // x 500 ms
const int kCallTimeoutMs = 30000;

QString portFile(const QString &profileDir)
{
    return profileDir + QStringLiteral("/DevToolsActivePort");
}

} // namespace

EdgeSession::EdgeSession(QObject *parent)
    : QObject(parent)
{
    // The browser is on this machine: never send these through a proxy.
    m_net.setProxy(QNetworkProxy::NoProxy);
    m_ws.setProxy(QNetworkProxy::NoProxy);

    connect(&m_ws, &QWebSocket::textMessageReceived, this, [this](const QString &msg) {
        const QJsonObject o = QJsonDocument::fromJson(msg.toUtf8()).object();
        const int id = o.value(QStringLiteral("id")).toInt(-1);
        if (id < 0)
            return;   // an event; none are subscribed to
        auto cb = m_pending.take(id);
        if (!cb)
            return;
        const QJsonObject err = o.value(QStringLiteral("error")).toObject();
        cb(o.value(QStringLiteral("result")).toObject(),
           err.isEmpty() ? QString() : err.value(QStringLiteral("message")).toString(QStringLiteral("error")));
    });
    connect(&m_ws, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), this, [this] {
        if (m_starting)
            fail(QStringLiteral("could not connect to the browser: ") + m_ws.errorString());
    });
    connect(&m_ws, &QWebSocket::disconnected, this, [this] {
        const bool was = m_ready;
        m_ready = false;
        auto pending = m_pending;
        m_pending.clear();
        for (auto &cb : pending)
            cb(QJsonObject(), QStringLiteral("the browser connection closed"));
        if (m_starting)
            fail(QStringLiteral("the browser closed the connection"));
        else if (was)
            emit disconnected();
    });
}

EdgeSession::~EdgeSession()
{
    m_ws.disconnect(this);
    m_ws.abort();
}

QString EdgeSession::findBrowser()
{
    QStringList candidates;
#ifdef Q_OS_WIN
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char *var : {"ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"}) {
        const QString base = env.value(QString::fromLatin1(var));
        if (base.isEmpty())
            continue;
        candidates << base + QStringLiteral("/Microsoft/Edge/Application/msedge.exe");
    }
    for (const char *var : {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"}) {
        const QString base = env.value(QString::fromLatin1(var));
        if (!base.isEmpty())
            candidates << base + QStringLiteral("/Google/Chrome/Application/chrome.exe");
    }
    for (const QString &name : {QStringLiteral("msedge"), QStringLiteral("chrome")}) {
        const QString p = QStandardPaths::findExecutable(name);
        if (!p.isEmpty())
            candidates << p;
    }
#elif defined(Q_OS_MACOS)
    candidates << QStringLiteral("/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge")
               << QStringLiteral("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome");
#else
    for (const QString &name : {QStringLiteral("microsoft-edge"), QStringLiteral("microsoft-edge-stable"),
                                QStringLiteral("google-chrome"), QStringLiteral("google-chrome-stable"),
                                QStringLiteral("chromium"), QStringLiteral("chromium-browser")}) {
        const QString p = QStandardPaths::findExecutable(name);
        if (!p.isEmpty())
            candidates << p;
    }
#endif
    for (const QString &c : candidates)
        if (QFileInfo(c).isExecutable() || QFileInfo::exists(c))
            return QDir::toNativeSeparators(c);
    return QString();
}

QString EdgeSession::browserPath() const
{
    return m_browserPath.isEmpty() ? findBrowser() : m_browserPath;
}

void EdgeSession::get(const QUrl &url, const QByteArray &verb,
                      std::function<void(int, const QByteArray &)> done)
{
    QNetworkRequest req(url);
    QNetworkReply *reply = m_net.sendCustomRequest(req, verb);
    // Qt 5.12 has no per-request transfer timeout; enforce one by hand.
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(kHttpTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        done(reply->error() == QNetworkReply::NoError ? status : 0, reply->readAll());
    });
}

int EdgeSession::readPortFile() const
{
    QFile f(portFile(m_profileDir));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    return QString::fromLatin1(f.readLine()).trimmed().toInt();
}

void EdgeSession::probe(int port, std::function<void(bool)> done)
{
    if (port <= 0) {
        done(false);
        return;
    }
    get(QUrl(QStringLiteral("http://127.0.0.1:%1/json/version").arg(port)), "GET",
        [done](int status, const QByteArray &) { done(status == 200); });
}

void EdgeSession::start()
{
    if (isConnected()) {
        emit connected();
        return;
    }
    if (m_starting)
        return;
    if (m_profileDir.isEmpty()) {
        fail(QStringLiteral("no profile folder set"));
        return;
    }
    m_starting = true;
    m_launched = false;
    QDir().mkpath(m_profileDir);

    // Already running with our profile (e.g. from an earlier start)?
    QPointer<EdgeSession> self(this);
    const int port = readPortFile();
    probe(port, [self, port](bool alive) {
        if (!self)
            return;
        if (alive)
            self->attach(port);
        else
            self->launch();
    });
}

void EdgeSession::launch()
{
    const QString exe = browserPath();
    if (exe.isEmpty()) {
        fail(QStringLiteral("Microsoft Edge (or Chrome) was not found. Set its path in Settings."));
        return;
    }
    QFile::remove(portFile(m_profileDir));   // stale from a previous run

    QStringList args;
    args << QStringLiteral("--remote-debugging-port=0")   // Edge picks a free port and writes it down
         << QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(m_profileDir)
         << QStringLiteral("--no-first-run")
         << QStringLiteral("--no-default-browser-check")
         << m_extraArgs;
    if (m_startUrl.isValid())
        args << m_startUrl.toString();

    emit status(tr("Starting %1...").arg(QFileInfo(exe).completeBaseName()));
    qint64 pid = 0;
    if (!QProcess::startDetached(exe, args, QString(), &pid)) {
        fail(QStringLiteral("could not start %1").arg(exe));
        return;
    }
    m_pid = pid;
    m_launched = true;
    waitForPort(kLaunchWaitSteps);
}

void EdgeSession::waitForPort(int attemptsLeft)
{
    QPointer<EdgeSession> self(this);
    QTimer::singleShot(500, this, [self, attemptsLeft] {
        if (!self || !self->m_starting)
            return;
        const int port = self->readPortFile();
        self->probe(port, [self, port, attemptsLeft](bool alive) {
            if (!self || !self->m_starting)
                return;
            if (alive) {
                self->attach(port);
            } else if (attemptsLeft > 0) {
                self->waitForPort(attemptsLeft - 1);
            } else {
                self->fail(QStringLiteral(
                    "the browser started but did not open its remote-control port. Your organisation may "
                    "turn this off (Edge policy \"RemoteDebuggingAllowed\"), or another Edge window with "
                    "the same profile is still closing. Use the copy & paste mode instead"));
            }
        });
    });
}

void EdgeSession::attach(int port)
{
    QPointer<EdgeSession> self(this);
    get(QUrl(QStringLiteral("http://127.0.0.1:%1/json/list").arg(port)), "GET",
        [self, port](int status, const QByteArray &body) {
        if (!self)
            return;
        if (status != 200) {
            self->fail(QStringLiteral("could not list the browser's tabs"));
            return;
        }
        const QJsonArray targets = QJsonDocument::fromJson(body).array();
        const QString host = self->m_startUrl.host();
        QJsonObject best, blank, anyPage;
        for (const QJsonValue &v : targets) {
            const QJsonObject t = v.toObject();
            if (t.value(QStringLiteral("type")).toString() != QLatin1String("page"))
                continue;
            const QUrl u(t.value(QStringLiteral("url")).toString());
            if (anyPage.isEmpty())
                anyPage = t;
            if (!host.isEmpty() && u.host() == host && best.isEmpty())
                best = t;
            const QString s = u.toString();
            if (blank.isEmpty() && (s == QLatin1String("about:blank") || s.contains(QLatin1String("newtab"))))
                blank = t;
        }
        const QString wsKey = QStringLiteral("webSocketDebuggerUrl");
        if (!best.isEmpty()) {
            self->openSocket(QUrl(best.value(wsKey).toString()), false);
        } else if (!blank.isEmpty()) {
            self->openSocket(QUrl(blank.value(wsKey).toString()), true);
        } else if (self->m_launched && !anyPage.isEmpty()) {
            // We just opened it with the Copilot URL; it may be on the
            // sign-in page, on another host.
            self->openSocket(QUrl(anyPage.value(wsKey).toString()), false);
        } else {
            // Open a new tab with Copilot (newer browsers want PUT here).
            QUrl newUrl(QStringLiteral("http://127.0.0.1:%1/json/new").arg(port));
            newUrl.setQuery(QString::fromLatin1(self->m_startUrl.toEncoded()));
            self->get(newUrl, "PUT", [self](int status, const QByteArray &body) {
                if (!self)
                    return;
                const QJsonObject t = QJsonDocument::fromJson(body).object();
                if (status != 200 || t.isEmpty()) {
                    self->fail(QStringLiteral("could not open a tab for Copilot"));
                    return;
                }
                self->openSocket(QUrl(t.value(QStringLiteral("webSocketDebuggerUrl")).toString()), false);
            });
        }
    });
}

void EdgeSession::openSocket(const QUrl &ws, bool navigate)
{
    if (!ws.isValid()) {
        fail(QStringLiteral("the tab has no remote-control address"));
        return;
    }
    m_navigateOnOpen = navigate;
    QPointer<EdgeSession> self(this);
    // Single-shot connection for this attempt.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(&m_ws, &QWebSocket::connected, this, [self, conn] {
        QObject::disconnect(*conn);
        if (!self)
            return;
        self->m_ready = true;
        // Makes the page behave as if its window had focus, so editors
        // accept typed text and Enter even while the user works elsewhere.
        self->call(QStringLiteral("Emulation.setFocusEmulationEnabled"),
                   QJsonObject{{QStringLiteral("enabled"), true}}, [](const QJsonObject &, const QString &) {});
        if (self->m_navigateOnOpen && self->m_startUrl.isValid())
            self->call(QStringLiteral("Page.navigate"),
                       QJsonObject{{QStringLiteral("url"), self->m_startUrl.toString()}},
                       [](const QJsonObject &, const QString &) {});
        self->m_starting = false;
        emit self->status(tr("Connected to the browser."));
        emit self->connected();
    });
    m_ws.open(ws);
}

void EdgeSession::fail(const QString &why)
{
    m_starting = false;
    m_ready = false;
    emit failed(why);
}

void EdgeSession::disconnectFromBrowser()
{
    m_ready = false;
    m_ws.close();
}

void EdgeSession::call(const QString &method, const QJsonObject &params,
                       std::function<void(const QJsonObject &, const QString &)> done)
{
    if (!isConnected()) {
        done(QJsonObject(), QStringLiteral("not connected to the browser"));
        return;
    }
    const int id = m_nextId++;
    m_pending.insert(id, done);
    const QJsonObject msg{{QStringLiteral("id"), id}, {QStringLiteral("method"), method},
                          {QStringLiteral("params"), params}};
    m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
    QPointer<EdgeSession> self(this);
    QTimer::singleShot(kCallTimeoutMs, this, [self, id] {
        if (!self)
            return;
        auto cb = self->m_pending.take(id);
        if (cb)
            cb(QJsonObject(), QStringLiteral("the browser did not answer"));
    });
}

void EdgeSession::evaluate(const QString &script, std::function<void(const QVariant &)> done)
{
    call(QStringLiteral("Runtime.evaluate"),
         QJsonObject{{QStringLiteral("expression"), script},
                     {QStringLiteral("returnByValue"), true},
                     {QStringLiteral("userGesture"), true}},
         [done](const QJsonObject &result, const QString &error) {
        if (!error.isEmpty() || result.contains(QStringLiteral("exceptionDetails"))) {
            done(QVariant());
            return;
        }
        done(result.value(QStringLiteral("result")).toObject().value(QStringLiteral("value")).toVariant());
    });
}

void EdgeSession::pressEnter()
{
    const QJsonObject down{{QStringLiteral("type"), QStringLiteral("keyDown")},
                           {QStringLiteral("key"), QStringLiteral("Enter")},
                           {QStringLiteral("code"), QStringLiteral("Enter")},
                           {QStringLiteral("windowsVirtualKeyCode"), 13},
                           {QStringLiteral("nativeVirtualKeyCode"), 13},
                           {QStringLiteral("text"), QStringLiteral("\r")},
                           {QStringLiteral("unmodifiedText"), QStringLiteral("\r")}};
    QJsonObject up = down;
    up[QStringLiteral("type")] = QStringLiteral("keyUp");
    up.remove(QStringLiteral("text"));
    up.remove(QStringLiteral("unmodifiedText"));
    auto ignore = [](const QJsonObject &, const QString &) {};
    call(QStringLiteral("Input.dispatchKeyEvent"), down, ignore);
    call(QStringLiteral("Input.dispatchKeyEvent"), up, ignore);
}

void EdgeSession::insertText(const QString &text, std::function<void(bool)> done)
{
    call(QStringLiteral("Input.insertText"), QJsonObject{{QStringLiteral("text"), text}},
         [done](const QJsonObject &, const QString &error) { done(error.isEmpty()); });
}
