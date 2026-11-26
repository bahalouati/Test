#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>

// Runs one approved shell command at a time, without blocking the window,
// and kills it when the timeout passes.
class CommandRunner : public QObject
{
    Q_OBJECT
public:
    explicit CommandRunner(QObject *parent = nullptr);
    ~CommandRunner() override;

    void start(const QString &command, const QString &workingDir, int timeoutSec);
    void kill();
    bool isRunning() const;

    // "cmd.exe" on Windows, "/bin/bash" (or /bin/sh) elsewhere; said in the
    // prompt so Copilot writes commands for the right shell.
    static QString shellName();

    static constexpr int kMaxOutputBytes = 2 * 1024 * 1024;

signals:
    void output(const QString &chunk);   // live, for the log
    void finished(int exitCode, bool timedOut, bool failedToStart, const QString &allOutput);

private:
    void collect();

    QProcess *m_proc = nullptr;
    QTimer m_timer;
    QByteArray m_buffer;
    bool m_timedOut = false;
    bool m_done = false;
};
