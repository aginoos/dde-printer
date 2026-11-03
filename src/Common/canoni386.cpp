// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "canoni386.h"

#include "safeprocess.h"

#include <QCoreApplication>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("CanonI386", text);
}

bool listsLine(const QString &output, const QString &wanted)
{
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.trimmed() == wanted)
            return true;
    }
    return false;
}

int countInstalled(const QString &statusOutput)
{
    int count = 0;
    const QStringList lines = statusOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.trimmed() == QLatin1String("ii"))
            ++count;
    }
    return count;
}

CanonI386::Runner &runner()
{
    static CanonI386::Runner instance;
    return instance;
}

SafeProcess::Result runCommand(const QString &program, const QStringList &args)
{
    return runner() ? runner()(program, args) : SafeProcess::run(program, args);
}

}  // namespace

namespace CanonI386 {

void setRunner(Runner newRunner)
{
    runner() = std::move(newRunner);
}

QString fixId()
{
    return QStringLiteral("canon_lbp2900");
}

QStringList requiredLibraries()
{
    return {QStringLiteral("libpopt0:i386"), QStringLiteral("libxml2:i386"),
            QStringLiteral("libstdc++6:i386")};
}

Status parseStatus(const QString &foreignArchitectures, const QString &libraryStatus, int expectedLibraries)
{
    Status status;
    status.architectureEnabled = listsLine(foreignArchitectures, QStringLiteral("i386"));
    status.librariesInstalled = status.architectureEnabled && expectedLibraries > 0
        && countInstalled(libraryStatus) == expectedLibraries;
    return status;
}

bool isReady(const Status &status)
{
    return status.architectureEnabled && status.librariesInstalled;
}

Status queryStatus()
{
    const SafeProcess::Result arch = runCommand(
        QStringLiteral("dpkg"), {QStringLiteral("--print-foreign-architectures")});
    const QStringList libraries = requiredLibraries();
    const QStringList queryArgs = QStringList{QStringLiteral("-W"), QStringLiteral("-f"),
                                              QStringLiteral("${db:Status-Abbrev}\\n")}
        + libraries;
    // The exit code is not 0 when a package is unknown; only the output counts.
    const SafeProcess::Result libs = runCommand(QStringLiteral("dpkg-query"), queryArgs);
    return parseStatus(arch.ok() ? arch.out : QString(), libs.out, libraries.size());
}

bool needsConfirmation(const QString &repairFixId, const Status &status)
{
    return repairFixId == fixId() && !isReady(status);
}

bool needsConfirmationForDriver(const Status &status)
{
    return !isReady(status);
}

QString confirmTitle()
{
    return tr("32-bit support required");
}

QString confirmMessage()
{
    return tr("The Canon CAPT driver needs 32-bit (i386) runtime libraries. "
              "32-bit support will be enabled and the required libraries installed. "
              "This needs the administrator password.");
}

QString confirmAcceptText()
{
    return tr("Enable and Install");
}

QString confirmCancelText()
{
    return tr("Cancel");
}

QString cancelledText()
{
    return tr("The Canon printer was not added because the 32-bit (i386) support it needs was not enabled.");
}

QString outcomeText(TroubleshootService::Outcome outcome)
{
    using TroubleshootService::Outcome;
    switch (outcome) {
    case Outcome::ArchitectureFailed:
        return tr("Could not enable 32-bit (i386) support.");
    case Outcome::UpdateFailed:
        return tr("Could not update the package lists. Please check the network connection and try again.");
    case Outcome::InstallFailed:
        return tr("Could not install the 32-bit libraries needed by the Canon driver.");
    case Outcome::PackageManagerBusy:
        return tr("Another program is using the package manager. Please try again in a few minutes.");
    default:
        return QString();
    }
}

}  // namespace CanonI386
