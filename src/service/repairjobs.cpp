// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "repairjobs.h"

#include "commandrunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>


namespace {

const char kCupsUnit[] = "cups.service";
const char kCcpdUnit[] = "ccpd.service";
const char kFilterDir[] = "/usr/lib/cups/filter";

using BoolDone = std::function<void(bool)>;

void systemctl(QObject *owner, const QStringList &args, BoolDone done)
{
    const QString tool = CommandRunner::findTool("systemctl");
    if (tool.isEmpty()) {
        done(false);
        return;
    }
    CommandRunner::run(owner, tool, args,
                       [done](const CommandResult &result) { done(result.ok()); },
                       RepairJobs::kStepTimeoutMs);
}

// Runs the steps in order and ignores their results; `next` runs afterwards.
void runStepsIgnoringResults(QObject *owner, QList<QStringList> steps, std::function<void()> next)
{
    if (steps.isEmpty()) {
        next();
        return;
    }
    const QStringList step = steps.takeFirst();
    systemctl(owner, step, [owner, steps, next](bool) {
        runStepsIgnoringResults(owner, steps, next);
    });
}

void repairCups(QObject *owner, BoolDone done)
{
    const QStringList restart{"restart", kCupsUnit};
    const QStringList check{"is-active", "--quiet", kCupsUnit};

    systemctl(owner, restart, [owner, check, done](bool) {
        systemctl(owner, check, [owner, check, done](bool active) {
            if (active) {
                done(true);
                return;
            }
            // Escalate: clear a failed state, make sure it starts at boot, try again.
            const QList<QStringList> escalation = {
                QStringList{"reset-failed", kCupsUnit},
                QStringList{"enable", kCupsUnit},
                QStringList{"restart", kCupsUnit}};
            runStepsIgnoringResults(owner, escalation,
                [owner, check, done]() { systemctl(owner, check, done); });
        });
    });
}

void runTool(QObject *owner, const QString &name, const QStringList &args, int timeoutMs,
             std::function<void(const CommandResult &)> done)
{
    const QString tool = CommandRunner::findTool(name);
    if (tool.isEmpty()) {
        done(CommandResult());
        return;
    }
    CommandRunner::run(owner, tool, args, done, timeoutMs);
}

// The 32-bit libraries come from fixed package names; the architecture is only
// checked, never added, so the repair cannot change the system's multiarch setup.
void installCanonLibraries(QObject *owner, BoolDone done)
{
    runTool(owner, "dpkg", RepairJobs::foreignArchitectureArgs(), RepairJobs::kStepTimeoutMs,
            [owner, done](const CommandResult &arch) {
        if (!arch.ok() || !RepairJobs::hasForeignArchitecture(QString::fromUtf8(arch.output), "i386")) {
            done(false);
            return;
        }
        runTool(owner, "apt-get", RepairJobs::aptInstallArgs(RepairJobs::canonI386Packages()),
                RepairJobs::kInstallTimeoutMs,
                [done](const CommandResult &install) { done(install.ok()); });
    });
}

void repairCanon(QObject *owner, BoolDone done)
{
    systemctl(owner, QStringList{"enable", "--now", kCcpdUnit}, [owner, done](bool) {
        installCanonLibraries(owner, [owner, done](bool installed) {
            if (!installed) {
                done(false);
                return;
            }
            systemctl(owner, QStringList{"is-active", "--quiet", kCcpdUnit}, done);
        });
    });
}

bool repairBrotherFilters()
{
    const QDir dir(kFilterDir);
    bool allOk = true;
    const QFileInfoList files = dir.entryInfoList({"brother_*"}, QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : files) {
        if (info.isSymLink() || !RepairJobs::isRepairableFilterName(info.fileName()))
            continue;
        const auto wanted = RepairJobs::withoutGroupOtherWrite(info.permissions());
        if (wanted != info.permissions())
            allOk = QFile::setPermissions(info.absoluteFilePath(), wanted) && allOk;
    }
    return allOk;
}

}  // namespace

namespace RepairJobs {

bool isKnownFix(const QString &fixId)
{
    return fixId == QLatin1String("cups_service") || fixId == QLatin1String("canon_lbp2900")
        || fixId == QLatin1String("brother_dcp_mfc");
}

QStringList canonI386Packages()
{
    return {QStringLiteral("libpopt0:i386"), QStringLiteral("libxml2:i386")};
}

QStringList foreignArchitectureArgs()
{
    return {QStringLiteral("--print-foreign-architectures")};
}

bool hasForeignArchitecture(const QString &dpkgOutput, const QString &arch)
{
    const QStringList lines = dpkgOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.trimmed() == arch)
            return true;
    }
    return false;
}

QStringList aptInstallArgs(const QStringList &packages)
{
    return QStringList{QStringLiteral("-y"), QStringLiteral("-q"),
                       QStringLiteral("--no-install-recommends"), QStringLiteral("install")}
        + packages;
}

QFileDevice::Permissions withoutGroupOtherWrite(QFileDevice::Permissions permissions)
{
    return permissions & ~(QFileDevice::WriteGroup | QFileDevice::WriteOther);
}

bool isRepairableFilterName(const QString &fileName)
{
    static const QRegularExpression re(QStringLiteral("\\Abrother_[A-Za-z0-9_.-]{1,100}\\z"));
    return re.match(fileName).hasMatch();
}

void run(QObject *owner, const QString &fixId, std::function<void(int)> done)
{
    const BoolDone finish = [done](bool ok) { done(ok ? 0 : 1); };
    if (fixId == QLatin1String("cups_service"))
        repairCups(owner, finish);
    else if (fixId == QLatin1String("canon_lbp2900"))
        repairCanon(owner, finish);
    else if (fixId == QLatin1String("brother_dcp_mfc"))
        finish(repairBrotherFilters());
    else
        finish(false);
}

}  // namespace RepairJobs
