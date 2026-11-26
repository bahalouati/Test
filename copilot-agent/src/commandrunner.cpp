#include "commandrunner.h"

#include <QFileInfo>

CommandRunner::CommandRunner(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        kill();
    });
}

CommandRunner::~CommandRunner()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

QString CommandRunner::shellName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("cmd.exe");
#else
    return QFileInfo::exists(QStringLiteral("/bin/bash")) ? QStringLiteral("/bin/bash")
                                                         : QStringLiteral("/bin/sh");
#endif
}

bool CommandRunner::isRunning() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

void CommandRunner::start(const QString &command, const QString &workingDir, int timeoutSec)
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->deleteLater();
    }
    m_buffer.clear();
    m_timedOut = false;
    m_done = false;

    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(workingDir);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    // Nothing can answer a prompt, so make stdin hit EOF straight away.
    m_proc->setStandardInputFile(QProcess::nullDevice());

    connect(m_proc, &QProcess::readyRead, this, &CommandRunner::collect);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        collect();
        m_timer.stop();
        if (m_done)
            return;
        m_done = true;
        const int exitCode = status == QProcess::CrashExit && code == 0 ? -1 : code;
        emit finished(exitCode, m_timedOut, false, QString::fromLocal8Bit(m_buffer));
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart || m_done)
            return;
        m_done = true;
        m_timer.stop();
        emit finished(-1, false, true, m_proc->errorString());
    });

#ifdef Q_OS_WIN
    // Pass the line through untouched: cmd /C has its own quoting rules and
    // QProcess's argument quoting would break them.
    m_proc->setProgram(QStringLiteral("cmd.exe"));
    m_proc->setNativeArguments(QStringLiteral("/D /S /C \"") + command + QLatin1Char('"'));
#else
    m_proc->setProgram(shellName());
    m_proc->setArguments({QStringLiteral("-c"), command});
#endif
    m_timer.start(qMax(1, timeoutSec) * 1000);
    m_proc->start();
}

void CommandRunner::collect()
{
    if (!m_proc)
        return;
    const QByteArray chunk = m_proc->readAll();
    if (chunk.isEmpty())
        return;
    if (m_buffer.size() < kMaxOutputBytes)
        m_buffer += chunk.left(kMaxOutputBytes - m_buffer.size());
    emit output(QString::fromLocal8Bit(chunk));
}

void CommandRunner::kill()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        m_proc->kill();
}
