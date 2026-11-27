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
const int kLaunchWaitSteps = 180;     // x 500 ms: a first start on a slow or managed PC can take a while
const int kCallTimeoutMs = 30000;
const int kHealthTimeoutMs = 5000;

// Copilot moves between addresses (m365.cloud.microsoft redirects to
// copilot.cloud.microsoft, and so on); any of them is "the Copilot tab".
bool sameService(const QString &host, const QString &startHost)
{
    if (host.isEmpty() || startHost.isEmpty())
        return false;
    if (host == startHost)
        return true;
    static const QStringList copilotHosts = {
        QStringLiteral("m365.cloud.microsoft"), QStringLiteral("copilot.cloud.microsoft"),
        QStringLiteral("copilot.microsoft.com"), QStringLiteral("www.microsoft365.com"),
        QStringLiteral("microsoft365.com"), QStringLiteral("www.office.com"),
    };
    auto isCopilot = [](const QString &h) {
        return copilotHosts.contains(h) || h.endsWith(QLatin1String(".cloud.microsoft"));
    };
    return isCopilot(host) && isCopilot(startHost);
}

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
        if (m_switching)
            m_switching = false;   // closed on purpose to change tabs
        else if (m_starting)
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
    // Detached, with its console output discarded: the browser outlives
    // the app and must not hold on to (or spam) the app's own streams.
    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments(args);
    proc.setStandardInputFile(QProcess::nullDevice());
    proc.setStandardOutputFile(QProcess::nullDevice());
    proc.setStandardErrorFile(QProcess::nullDevice());
    qint64 pid = 0;
    if (!proc.startDetached(&pid)) {
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
    m_port = port;
    m_triedNewTab = false;
    QPointer<EdgeSession> self(this);
    get(QUrl(QStringLiteral("http://127.0.0.1:%1/json/list").arg(port)), "GET",
        [self, port](int status, const QByteArray &body) {
        if (!self)
            return;
        if (status != 200) {
            self->fail(QStringLiteral("could not list the browser's tabs"));
            return;
        }
        // Only ever use a tab that is showing Copilot (or, right after we
        // launched it, Microsoft's sign-in on the way there). Never borrow
        // some other blank/new tab: Edge keeps hidden preloaded pages that
        // can be frozen and then never run anything.
        const QString host = self->m_startUrl.host();
        QJsonObject best;
        for (const QJsonValue &v : QJsonDocument::fromJson(body).array()) {
            const QJsonObject t = v.toObject();
            if (t.value(QStringLiteral("type")).toString() != QLatin1String("page"))
                continue;
            const QUrl u(t.value(QStringLiteral("url")).toString());
            const bool copilot = sameService(u.host(), host);
            const bool signIn = self->m_launched && u.host().contains(QLatin1String("login.microsoftonline"));
            if (copilot || signIn) {
                best = t;
                if (copilot)
                    break;
            }
        }
        if (!best.isEmpty())
            self->openSocket(best);
        else
            self->openNewTab(port);
    });
}

void EdgeSession::openNewTab(int port)
{
    m_triedNewTab = true;
    QPointer<EdgeSession> self(this);
    QUrl newUrl(QStringLiteral("http://127.0.0.1:%1/json/new").arg(port));
    newUrl.setQuery(QString::fromLatin1(m_startUrl.isValid() ? m_startUrl.toEncoded() : QByteArray("about:blank")));
    // Newer browsers want PUT here; older ones GET.
    get(newUrl, "PUT", [self, newUrl](int status, const QByteArray &body) {
        if (!self)
            return;
        auto use = [self](const QByteArray &b) {
            const QJsonObject t = QJsonDocument::fromJson(b).object();
            if (t.isEmpty()) {
                self->fail(QStringLiteral("could not open a tab for Copilot"));
                return;
            }
            self->openSocket(t);
        };
        if (status == 200) {
            use(body);
            return;
        }
        self->get(newUrl, "GET", [self, use](int status2, const QByteArray &body2) {
            if (!self)
                return;
            if (status2 != 200)
                self->fail(QStringLiteral("could not open a tab for Copilot"));
            else
                use(body2);
        });
    });
}

void EdgeSession::wake()
{
    if (!isConnected())
        return;
    // Undo Edge's tab freezing ("sleeping tabs") and show the tab, so its
    // scripts run; pretend the window has focus so editors accept input.
    auto ignore = [](const QJsonObject &, const QString &) {};
    call(QStringLiteral("Page.setWebLifecycleState"), QJsonObject{{QStringLiteral("state"), QStringLiteral("active")}}, ignore);
    call(QStringLiteral("Page.bringToFront"), QJsonObject(), ignore);
    call(QStringLiteral("Emulation.setFocusEmulationEnabled"), QJsonObject{{QStringLiteral("enabled"), true}}, ignore);
}

void EdgeSession::openSocket(const QJsonObject &target)
{
    const QUrl ws(target.value(QStringLiteral("webSocketDebuggerUrl")).toString());
    const QString pageUrl = target.value(QStringLiteral("url")).toString();
    if (!ws.isValid() || ws.isEmpty()) {
        fail(QStringLiteral("the tab has no remote-control address"));
        return;
    }
    QPointer<EdgeSession> self(this);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(&m_ws, &QWebSocket::connected, this, [self, conn, pageUrl] {
        QObject::disconnect(*conn);
        if (!self)
            return;
        self->m_ready = true;
        self->wake();
        // Health check: a tab that cannot run a trivial script is no use.
        auto answered = std::make_shared<bool>(false);
        self->call(QStringLiteral("Runtime.evaluate"),
                   QJsonObject{{QStringLiteral("expression"), QStringLiteral("1+1")},
                               {QStringLiteral("returnByValue"), true}},
                   [self, answered, pageUrl](const QJsonObject &r, const QString &err) {
            if (*answered || !self)
                return;
            *answered = true;
            if (!err.isEmpty() || r.value(QStringLiteral("result")).toObject().value(QStringLiteral("value")).toInt() != 2) {
                self->retryOrFail(QStringLiteral("the browser tab did not run scripts"));
                return;
            }
            self->m_starting = false;
            emit self->status(tr("Connected to the browser (%1).").arg(pageUrl.left(80)));
            emit self->connected();
        });
        QTimer::singleShot(kHealthTimeoutMs, self, [self, answered] {
            if (!self || *answered)
                return;
            *answered = true;
            self->retryOrFail(QStringLiteral("the browser tab did not respond"));
        });
    });
    emit status(tr("Attaching to %1").arg(pageUrl.left(80)));
    m_ws.open(ws);
}

void EdgeSession::retryOrFail(const QString &why)
{
    if (m_triedNewTab || m_port <= 0) {
        fail(why);
        return;
    }
    // Drop this tab quietly and use a fresh one instead.
    m_switching = true;
    m_ready = false;
    m_ws.abort();
    openNewTab(m_port);
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
