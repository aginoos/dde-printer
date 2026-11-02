// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "commandrunner.h"

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

#include <memory>

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

QString findTool(const QString &name)
{
    static const QStringList dirs = {"/usr/sbin", "/usr/bin", "/sbin", "/bin"};
    for (const QString &dir : dirs) {
        const QFileInfo info(dir + "/" + name);
        if (info.isFile() && info.isExecutable())
            return info.filePath();
    }
    return QString();
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

    const auto settled = std::make_shared<bool>(false);
    const auto finish = [proc, settled, done](bool started, bool timedOut) {
        if (*settled)
            return;
        *settled = true;

        CommandResult result;
        result.started = started;
        result.timedOut = timedOut;
        if (started && !timedOut && proc->exitStatus() == QProcess::NormalExit)
            result.exitCode = proc->exitCode();
        result.output = proc->readAll();
        proc->deleteLater();
        done(result);
    };

    QObject::connect(proc, &QProcess::finished, proc,
                     [finish](int, QProcess::ExitStatus) { finish(true, false); });
    QObject::connect(proc, &QProcess::errorOccurred, proc,
                     [finish](QProcess::ProcessError error) {
                         if (error == QProcess::FailedToStart)
                             finish(false, false);
                     });
    QTimer::singleShot(timeoutMs, proc, [proc, finish]() {
        if (proc->state() == QProcess::NotRunning)
            return;
        proc->kill();
        proc->waitForFinished(1000);
        finish(true, true);
    });

    proc->start(program, args);
}

}
