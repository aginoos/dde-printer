// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modelcheck.h"
#include "safeprocess.h"

#include <QDir>
#include <QFileInfo>

namespace {

constexpr int kQueryTimeoutMs = 5000;

ModelCheckRunner::CommandRunner g_commandRunner;

SafeProcess::Result runQuery(const QString &program, const QStringList &args)
{
    return g_commandRunner ? g_commandRunner(program, args, kQueryTimeoutMs)
                           : SafeProcess::run(program, args, kQueryTimeoutMs);
}

bool systemctlSucceeds(const QString &verb, const QString &unit)
{
    return runQuery("systemctl", {verb, "--quiet", "--", unit}).ok();
}

bool packageInstalled(const QString &package)
{
    const SafeProcess::Result r = runQuery(
        "dpkg-query", {"-W", "-f=${Status}", "--", package});
    return r.ok() && ModelCheckRunner::isInstalledStatus(r.out);
}

}  // namespace

namespace ModelCheckRunner {

CommandRunner setCommandRunner(CommandRunner runner)
{
    CommandRunner previous = std::move(g_commandRunner);
    g_commandRunner = std::move(runner);
    return previous;
}

bool isInstalledStatus(const QString &statusOutput)
{
    return statusOutput.trimmed() == QLatin1String("install ok installed");
}

bool hasUnsafeWrite(QFileDevice::Permissions permissions)
{
    return permissions & (QFileDevice::WriteGroup | QFileDevice::WriteOther);
}

bool filesAreSafe(const QString &dir, const QString &glob)
{
    const QDir directory(dir);
    const QFileInfoList entries = directory.entryInfoList(
        {glob}, QDir::Files | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        if (!info.exists() || hasUnsafeWrite(info.permissions()))
            return false;
    }
    return true;
}

bool passes(const ModelCheck &check)
{
    switch (check.type) {
    case ModelCheck::Type::ServiceActive:
        return systemctlSucceeds("is-active", check.target);
    case ModelCheck::Type::ServiceEnabled:
        return systemctlSucceeds("is-enabled", check.target);
    case ModelCheck::Type::PackageInstalled:
        return packageInstalled(check.target);
    case ModelCheck::Type::FilesNotWritable:
        return filesAreSafe(check.target, check.pattern);
    }
    return false;
}

QList<ModelCheck> failedChecks(const QList<ModelCheck> &checks)
{
    QList<ModelCheck> failed;
    for (const ModelCheck &check : checks) {
        if (!passes(check))
            failed.append(check);
    }
    return failed;
}

}  // namespace ModelCheckRunner
