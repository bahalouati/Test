#pragma once

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QWebSocket>

#include <functional>

// Drives Microsoft Edge (or Chrome) through its DevTools protocol, so
// Copilot runs in a real, current browser instead of Qt 5's old built-in one.
//
// Edge is started with its own profile folder and remote debugging on a
// local port chosen by Edge itself. The user signs in to Microsoft 365 once in
// that window; the profile keeps the session. If Edge is already running
// with that profile from an earlier start, it is reused.
//
// The debugging port listens on 127.0.0.1 only. While it is open, other
// programs on this computer could drive that Edge window too, as they
// could any browser started this way.
class EdgeSession : public QObject
{
    Q_OBJECT
public:
    explicit EdgeSession(QObject *parent = nullptr);
    ~EdgeSession() override;

    void setBrowserPath(const QString &path) { m_browserPath = path; }   // empty: find Edge, then Chrome
    void setProfileDir(const QString &dir) { m_profileDir = dir; }
    void setStartUrl(const QString &url) { m_startUrl = QUrl(url); }
    void setExtraArguments(const QStringList &args) { m_extraArgs = args; }

    // Edge, then Chrome, in the usual install places and on PATH.
    static QString findBrowser();

    bool isConnected() const { return m_ws.state() == QAbstractSocket::ConnectedState && m_ready; }
    bool isStarting() const { return m_starting; }
    QString browserPath() const;
    qint64 launchedPid() const { return m_pid; }

public slots:
    // Connects, launching the browser first if needed. Ends in connected()
    // or failed(). Safe to call while already connected or starting.
    void start();
    void disconnectFromBrowser();

public:
    // DevTools calls. done() always runs, with an error string on failure.
    void call(const QString &method, const QJsonObject &params,
              std::function<void(const QJsonObject &result, const QString &error)> done);
    // Runs a script in the page; done(invalid QVariant) on any error.
    void evaluate(const QString &script, std::function<void(const QVariant &)> done);
    // Real key press and real text input, as if typed by the user.
    void pressEnter();
    void insertText(const QString &text, std::function<void(bool)> done);

signals:
    void connected();
    void failed(const QString &why);
    void disconnected();
    void status(const QString &message);

private:
    int readPortFile() const;
    void probe(int port, std::function<void(bool)> done);
    void launch();
    void waitForPort(int attemptsLeft);
    void attach(int port);
    void openSocket(const QUrl &ws, bool navigate);
    void fail(const QString &why);
    void get(const QUrl &url, const QByteArray &verb, std::function<void(int status, const QByteArray &body)> done);

    QString m_browserPath;
    QString m_profileDir;
    QUrl m_startUrl;
    QStringList m_extraArgs;
    QNetworkAccessManager m_net;
    QWebSocket m_ws;
    bool m_ready = false;
    bool m_starting = false;
    bool m_launched = false;
    bool m_navigateOnOpen = false;
    qint64 m_pid = 0;
    int m_nextId = 1;
    QHash<int, std::function<void(const QJsonObject &, const QString &)>> m_pending;
};
