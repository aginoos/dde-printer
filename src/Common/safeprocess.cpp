// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "safeprocess.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QEventLoop>
#include <QTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(SAFEPROCESS, "org.deepin.dde-printer.safeprocess")

namespace SafeProcess {

namespace {

const QStringList kToolDirs = {"/usr/bin", "/usr/sbin", "/bin", "/sbin", "/usr/lib/cups/backend"};

QProcessEnvironment cleanEnvironment()
{
    QProcessEnvironment env;
    env.insert("PATH", "/usr/sbin:/usr/bin:/sbin:/bin");
    env.insert("LANG", "C.UTF-8");
    env.insert("LC_ALL", "C.UTF-8");
    return env;
}

bool matches(const QString &text, const char *pattern)
{
    const QRegularExpression re(QString::fromLatin1(pattern));
    return re.match(text).hasMatch();
}

bool prepare(QProcess &proc, const QString &program, const QStringList &args, Result &result)
{
    const QString path = findTool(program);
    if (path.isEmpty()) {
        qCWarning(SAFEPROCESS) << "tool not found:" << program;
        return false;
    }
    proc.setProcessEnvironment(cleanEnvironment());
    proc.start(path, args);
    result.started = proc.waitForStarted(5000);
    return result.started;
}

void collect(QProcess &proc, Result &result)
{
    result.out = QString::fromUtf8(proc.readAllStandardOutput());
    result.err = QString::fromUtf8(proc.readAllStandardError());
    result.exitCode = proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -1;
}

}  // namespace

QString findTool(const QString &name)
{
    if (name.startsWith('/')) {
        return QFileInfo(name).isExecutable() ? name : QString();
    }
    if (name.isEmpty() || name.contains('/')) {
        return QString();
    }
    for (const QString &dir : kToolDirs) {
        const QString candidate = dir + "/" + name;
        if (QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QString();
}

Result run(const QString &program, const QStringList &args, int timeoutMs)
{
    Result result;
    QProcess proc;
    if (!prepare(proc, program, args, result)) {
        return result;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        proc.kill();
        proc.waitForFinished(2000);
    }
    collect(proc, result);
    return result;
}

Result runKeepingUiAlive(const QString &program, const QStringList &args, int timeoutMs)
{
    Result result;
    QProcess proc;
    if (!prepare(proc, program, args, result)) {
        return result;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        result.timedOut = true;
        loop.quit();
    });
    if (proc.state() != QProcess::NotRunning) {
        timer.start(timeoutMs);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (result.timedOut) {
        proc.kill();
        proc.waitForFinished(2000);
    }
    collect(proc, result);
    return result;
}

bool isValidPackageName(const QString &name)
{
    return name.size() >= 2 && name.size() <= 128 && matches(name, "^[a-z0-9][a-z0-9+.-]+$");
}

bool isValidPackageVersion(const QString &version)
{
    return !version.isEmpty() && version.size() <= 128 && matches(version, "^[A-Za-z0-9][A-Za-z0-9.+~:_-]*$");
}

bool isValidHost(const QString &host)
{
    if (host.isEmpty() || host.size() > 253 || host.startsWith('-')) {
        return false;
    }
    QHostAddress addr;
    if (addr.setAddress(host)) {
        return true;
    }
    return matches(host, "^([A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)(\\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$");
}

bool isValidDeviceToken(const QString &token)
{
    return matches(token, "^[A-Za-z0-9_-]{1,64}$");
}

bool isValidQueueName(const QString &name)
{
    return matches(name, "^[A-Za-z0-9_][A-Za-z0-9_.+-]{0,126}$");
}

bool isSafeAbsolutePath(const QString &path)
{
    if (!path.startsWith('/') || path.size() > 4096) {
        return false;
    }
    if (path.contains(QChar('\0')) || matches(path, "[\\x00-\\x1f\\x7f]")) {
        return false;
    }
    return !path.split('/').contains("..");
}

}  // namespace SafeProcess
