// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "commandrunner.h"

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

#include <memory>

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

const char kSafePath[] = "/usr/sbin:/usr/bin:/sbin:/bin";

QProcessEnvironment fixedEnvironment()
{
    QProcessEnvironment env;
    env.insert("PATH", kSafePath);
    env.insert("LANG", "C");
    env.insert("LC_ALL", "C");
    return env;
}

}

namespace CommandRunner {

namespace {

std::function<QString(const QString &)> &toolResolver()
{
    static std::function<QString(const QString &)> resolver;
    return resolver;
}

}

void setToolResolver(std::function<QString(const QString &)> resolver)
{
    toolResolver() = std::move(resolver);
}

QString findTool(const QString &name)
{
    if (toolResolver())
        return toolResolver()(name);
    static const QStringList dirs = {"/usr/sbin", "/usr/bin", "/sbin", "/bin"};
    for (const QString &dir : dirs) {
        const QFileInfo info(dir + "/" + name);
        if (info.isFile() && info.isExecutable())
            return info.filePath();
    }
    return QString();
}

void appendCapped(QByteArray *buffer, const QByteArray &data, int maxBytes)
{
    const int room = maxBytes - buffer->size();
    if (room <= 0 || data.isEmpty())
        return;
    buffer->append(data.left(room));
}

namespace {

struct RunState {
    bool settled = false;
    bool timedOut = false;
    QByteArray output;
};

// The child leads its own process group so a timeout can stop everything the
// tool spawned, not just the tool itself.
void startInOwnGroup(QProcess *proc)
{
    proc->setChildProcessModifier([]() { ::setsid(); });
}

void stopGroup(QProcess *proc, const std::shared_ptr<RunState> &state)
{
    const pid_t leader = static_cast<pid_t>(proc->processId());
    if (leader <= 1)
        return;
    state->timedOut = true;
    ::kill(-leader, SIGTERM);
    QTimer::singleShot(kTerminateGraceMs, proc, [proc, leader]() {
        if (proc->state() != QProcess::NotRunning)
            ::kill(-leader, SIGKILL);
    });
}

}

void run(QObject *owner, const QString &program, const QStringList &args,
         Callback done, int timeoutMs)
{
    if (program.isEmpty()) {
        done(CommandResult());
        return;
    }

    auto *proc = new QProcess(owner);
    proc->setProcessEnvironment(fixedEnvironment());
    proc->setWorkingDirectory("/");
    proc->setProcessChannelMode(QProcess::MergedChannels);
    startInOwnGroup(proc);

    const auto state = std::make_shared<RunState>();
    const auto finish = [proc, state, done](bool started) {
        if (state->settled)
            return;
        state->settled = true;

        CommandResult result;
        result.started = started;
        result.timedOut = state->timedOut;
        if (started && !state->timedOut && proc->exitStatus() == QProcess::NormalExit)
            result.exitCode = proc->exitCode();
        appendCapped(&state->output, proc->readAll(), kMaxOutputBytes);
        result.output = state->output;
        proc->deleteLater();
        done(result);
    };

    // Drain continuously so a chatty tool can neither fill the pipe nor make
    // the service grow without bound.
    QObject::connect(proc, &QProcess::readyRead, proc, [proc, state]() {
        appendCapped(&state->output, proc->readAll(), kMaxOutputBytes);
    });
    QObject::connect(proc, &QProcess::finished, proc,
                     [finish](int, QProcess::ExitStatus) { finish(true); });
    QObject::connect(proc, &QProcess::errorOccurred, proc,
                     [finish](QProcess::ProcessError error) {
                         if (error == QProcess::FailedToStart)
                             finish(false);
                     });
    QTimer::singleShot(timeoutMs, proc, [proc, state]() {
        if (proc->state() == QProcess::NotRunning)
            return;
        stopGroup(proc, state);
    });

    proc->start(program, args);
}

}
