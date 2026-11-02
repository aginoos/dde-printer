// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "packageutil.h"
#include "safeprocess.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QRegularExpression>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(PACKAGEUTIL, "org.deepin.dde-printer.packageutil")

namespace PackageUtil {

namespace {

const char *const kLastoreServices[] = {"org.deepin.dde.Lastore1", "com.deepin.lastore"};
const QString kLocalDriverList = QStringLiteral("/etc/deepin/lastore-daemon/sources.list.d/local-driver.list");

bool dpkgSays(const QString &lhs, const QString &op, const QString &rhs)
{
    const auto res = SafeProcess::run("dpkg", {"--compare-versions", lhs, op, rhs}, 5000);
    return res.ok();
}

QStringList policyArguments(const QString &package)
{
    QStringList args {"policy"};
    // Lastore keeps the driver repository in its own list; use it only when it exists.
    if (lastoreAvailable() && QFile::exists(kLocalDriverList)) {
        args << "-o" << "Dir::Etc::SourceParts=/etc/apt/sources.list.d"
             << "-o" << "Dir::Etc::SourceList=" + kLocalDriverList;
    }
    args << package;
    return args;
}

}  // namespace

QString installedVersion(const QString &package)
{
    if (!SafeProcess::isValidPackageName(package)) {
        return QString();
    }
    const auto res = SafeProcess::run("dpkg-query", {"-W", "-f", "${db:Status-Status} ${Version}\n", "--", package}, 10000);
    if (!res.ok()) {
        return QString();
    }
    const QStringList fields = res.out.trimmed().split(' ', Qt::SkipEmptyParts);
    return (fields.size() == 2 && fields.first() == "installed") ? fields.at(1) : QString();
}

bool isInstalled(const QString &package)
{
    return !installedVersion(package).isEmpty();
}

VersionOrder compareVersions(const QString &lhs, const QString &rhs)
{
    if (!SafeProcess::isValidPackageVersion(lhs) || !SafeProcess::isValidPackageVersion(rhs)) {
        return VersionOrder::Unknown;
    }
    if (dpkgSays(lhs, "eq", rhs)) {
        return VersionOrder::Equal;
    }
    return dpkgSays(lhs, "lt", rhs) ? VersionOrder::Older : VersionOrder::Newer;
}

bool policyOffers(const QString &policyOutput, const QString &version)
{
    static const QRegularExpression candidateRe("^\\s*Candidate:\\s*(\\S+)", QRegularExpression::MultilineOption);
    static const QRegularExpression versionRe("^\\s+(?:\\*\\*\\*\\s+)?(\\S+)\\s+\\d+\\s*$", QRegularExpression::MultilineOption);

    const auto candidate = candidateRe.match(policyOutput);
    if (!candidate.hasMatch() || candidate.captured(1) == "(none)") {
        return false;
    }
    if (version.isEmpty()) {
        return true;
    }

    const int tableStart = policyOutput.indexOf("Version table:");
    if (tableStart < 0) {
        return false;
    }
    auto it = versionRe.globalMatch(policyOutput, tableStart);
    while (it.hasNext()) {
        if (it.next().captured(1) == version) {
            return true;
        }
    }
    return false;
}

bool isInstallable(const QString &package, const QString &version)
{
    if (!SafeProcess::isValidPackageName(package) || (!version.isEmpty() && !SafeProcess::isValidPackageVersion(version))) {
        return false;
    }
    const auto res = SafeProcess::runKeepingUiAlive("apt-cache", policyArguments(package), 8000);
    if (!res.ok()) {
        qCWarning(PACKAGEUTIL) << "apt-cache policy failed for" << package << res.exitCode << res.timedOut;
        return false;
    }
    return policyOffers(res.out, version);
}

bool lastoreAvailable()
{
    QDBusConnectionInterface *bus = QDBusConnection::systemBus().interface();
    if (!bus) {
        return false;
    }
    const QStringList activatable = bus->activatableServiceNames().value();
    for (const char *name : kLastoreServices) {
        const QString service = QString::fromLatin1(name);
        if (bus->isServiceRegistered(service).value() || activatable.contains(service)) {
            return true;
        }
    }
    return false;
}

}  // namespace PackageUtil
