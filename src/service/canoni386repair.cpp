// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "canoni386repair.h"

#include "commandrunner.h"
#include "../Common/repaircodes.h"
#include "repairjobs.h"

#include <QTimer>

#include <memory>

namespace {

const char kArch[] = "i386";
const char kCcpdUnit[] = "ccpd.service";
// Time added to a unit's runtime limit for systemd-run itself to return.
constexpr int kWaitSlackMs = 15000;

int g_lockRetryDelayMs = CanonI386Repair::kLockRetryDelayMs;

using Done = std::function<void(int)>;
using Reader = std::function<void(const CommandResult &)>;

struct PrivilegedResult {
    bool ok = false;
    bool lockBusy = false;
};
using PrivilegedDone = std::function<void(PrivilegedResult)>;

// State shared by the asynchronous steps of one run.
struct Context {
    QObject *owner = nullptr;
    bool addedArchitecture = false;
    Done done;
};
using ContextPtr = std::shared_ptr<Context>;

// Read-only tools run directly in the service (they only read dpkg's state).
void runDirect(QObject *owner, const QString &name, const QStringList &args, Reader done)
{
    const QString tool = CommandRunner::findTool(name);
    if (tool.isEmpty()) {
        done(CommandResult());
        return;
    }
    CommandRunner::run(owner, tool, args, done, RepairJobs::kStepTimeoutMs);
}

// Tools that change the system run as a transient unit, retried while another
// process holds the package lock.
void runPrivileged(QObject *owner, const QString &name, const QStringList &args, int timeoutMs,
                   int retriesLeft, PrivilegedDone done)
{
    const QString tool = CommandRunner::findTool(name);
    const QString launcher = CommandRunner::findTool("systemd-run");
    if (tool.isEmpty() || launcher.isEmpty()) {
        done(PrivilegedResult());
        return;
    }
    const QStringList unitArgs = CanonI386Repair::transientUnitArgs(tool, args, timeoutMs / 1000);
    CommandRunner::run(owner, launcher, unitArgs,
        [=](const CommandResult &result) {
            if (result.ok()) {
                done(PrivilegedResult{true, false});
                return;
            }
            const bool locked = CanonI386Repair::isLockContention(QString::fromUtf8(result.output));
            if (locked && retriesLeft > 0) {
                QTimer::singleShot(g_lockRetryDelayMs, owner, [=]() {
                    runPrivileged(owner, name, args, timeoutMs, retriesLeft - 1, done);
                });
                return;
            }
            done(PrivilegedResult{false, locked});
        },
        timeoutMs + kWaitSlackMs);
}

void finish(const ContextPtr &ctx, int code)
{
    ctx->done(code);
}

// Undo the architecture this run added when no i386 package ended up installed.
void failWithRollback(const ContextPtr &ctx, int code)
{
    if (!ctx->addedArchitecture) {
        finish(ctx, code);
        return;
    }
    runDirect(ctx->owner, "dpkg-query", CanonI386Repair::installedArchitectureQueryArgs(),
        [ctx, code](const CommandResult &query) {
            const int count = CanonI386Repair::countInstalledI386(QString::fromUtf8(query.output));
            // An unreadable database must not trigger a removal.
            if (!query.ok() || !CanonI386Repair::shouldRollbackArchitecture(true, count)) {
                finish(ctx, code);
                return;
            }
            runPrivileged(ctx->owner, "dpkg", {"--remove-architecture", kArch},
                          RepairJobs::kStepTimeoutMs, 0,
                          [ctx, code](PrivilegedResult) { finish(ctx, code); });
        });
}

void startCcpd(const ContextPtr &ctx)
{
    runDirect(ctx->owner, "systemctl", {"enable", "--now", kCcpdUnit}, [ctx](const CommandResult &) {
        runDirect(ctx->owner, "systemctl", {"is-active", "--quiet", kCcpdUnit},
                  [ctx](const CommandResult &active) {
            finish(ctx, active.ok() ? RepairCode::Ok : RepairCode::Failed);
        });
    });
}

void installLibraries(const ContextPtr &ctx)
{
    const QStringList packages = RepairJobs::canonI386Packages();
    runPrivileged(ctx->owner, "apt-get", RepairJobs::aptInstallArgs(packages),
                  RepairJobs::kInstallTimeoutMs, CanonI386Repair::kLockRetries,
                  [ctx, packages](PrivilegedResult install) {
        if (!install.ok) {
            failWithRollback(ctx, CanonI386Repair::codeForFailure(CanonI386Repair::Stage::Install,
                                                                 install.lockBusy));
            return;
        }
        // Do not trust the exit status alone: check that the libraries are really there.
        runDirect(ctx->owner, "dpkg-query", CanonI386Repair::statusQueryArgs(packages),
                  [ctx, packages](const CommandResult &status) {
            const bool present = CanonI386Repair::countInstalledPackages(QString::fromUtf8(status.output))
                == packages.size();
            if (!present) {
                failWithRollback(ctx, RepairCode::InstallFailed);
                return;
            }
            startCcpd(ctx);
        });
    });
}

void updateLists(const ContextPtr &ctx)
{
    runPrivileged(ctx->owner, "apt-get", CanonI386Repair::aptUpdateArgs(),
                  CanonI386Repair::kUpdateTimeoutMs, CanonI386Repair::kLockRetries,
                  [ctx](PrivilegedResult update) {
        if (!update.ok) {
            failWithRollback(ctx, CanonI386Repair::codeForFailure(CanonI386Repair::Stage::Update,
                                                                 update.lockBusy));
            return;
        }
        installLibraries(ctx);
    });
}

void addArchitecture(const ContextPtr &ctx)
{
    runPrivileged(ctx->owner, "dpkg", {"--add-architecture", kArch}, RepairJobs::kStepTimeoutMs,
                  CanonI386Repair::kLockRetries, [ctx](PrivilegedResult added) {
        if (!added.ok) {
            finish(ctx, CanonI386Repair::codeForFailure(CanonI386Repair::Stage::AddArchitecture,
                                                        added.lockBusy));
            return;
        }
        ctx->addedArchitecture = true;
        updateLists(ctx);
    });
}

// Reads whether i386 is enabled and the Canon libraries are installed.
void readState(QObject *owner, std::function<void(bool, bool)> done)
{
    runDirect(owner, "dpkg", RepairJobs::foreignArchitectureArgs(), [=](const CommandResult &arch) {
        const bool enabled = arch.ok()
            && RepairJobs::hasForeignArchitecture(QString::fromUtf8(arch.output), kArch);
        const QStringList packages = RepairJobs::canonI386Packages();
        if (!enabled) {
            done(false, false);
            return;
        }
        runDirect(owner, "dpkg-query", CanonI386Repair::statusQueryArgs(packages),
                  [=](const CommandResult &status) {
            done(true, CanonI386Repair::countInstalledPackages(QString::fromUtf8(status.output))
                           == packages.size());
        });
    });
}

}  // namespace

namespace CanonI386Repair {

Plan makePlan(bool architectureEnabled, bool librariesInstalled)
{
    Plan plan;
    plan.addArchitecture = !architectureEnabled;
    plan.installLibraries = !architectureEnabled || !librariesInstalled;
    return plan;
}

bool isLockContention(const QString &output)
{
    static const QStringList markers = {
        QStringLiteral("could not get lock"),
        QStringLiteral("unable to acquire the dpkg"),
        QStringLiteral("unable to lock"),
        QStringLiteral("was locked by another process"),
        QStringLiteral("is held by process"),
    };
    const QString text = output.toLower();
    for (const QString &marker : markers) {
        if (text.contains(marker))
            return true;
    }
    return false;
}

int countInstalledI386(const QString &queryOutput)
{
    int count = 0;
    const QStringList lines = queryOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // Every state except "not installed" (second letter 'n') keeps files or
        // configuration of the package, which blocks dpkg --remove-architecture.
        if (fields.size() == 2 && fields.at(0).size() >= 2 && fields.at(0).at(1) != QLatin1Char('n')
            && fields.at(1) == QLatin1String(kArch))
            ++count;
    }
    return count;
}

int countInstalledPackages(const QString &statusOutput)
{
    int count = 0;
    const QStringList lines = statusOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.trimmed() == QLatin1String("ii"))
            ++count;
    }
    return count;
}

bool shouldRollbackArchitecture(bool addedByThisRun, int installedI386Packages)
{
    return addedByThisRun && installedI386Packages == 0;
}

int codeForFailure(Stage stage, bool lockBusy)
{
    if (lockBusy)
        return RepairCode::PackageManagerBusy;
    switch (stage) {
    case Stage::AddArchitecture:
        return RepairCode::ArchitectureFailed;
    case Stage::Update:
        return RepairCode::UpdateFailed;
    case Stage::Install:
        return RepairCode::InstallFailed;
    }
    return RepairCode::Failed;
}

QStringList statusQueryArgs(const QStringList &packages)
{
    return QStringList{QStringLiteral("-W"), QStringLiteral("-f"),
                       QStringLiteral("${db:Status-Abbrev}\\n")}
        + packages;
}

QStringList installedArchitectureQueryArgs()
{
    return QStringList{QStringLiteral("-W"), QStringLiteral("-f"),
                       QStringLiteral("${db:Status-Abbrev} ${Architecture}\\n")};
}

QStringList aptUpdateArgs()
{
    return QStringList{QStringLiteral("-q"), QStringLiteral("-o"),
                       QStringLiteral("DPkg::Lock::Timeout=%1").arg(kLockWaitSeconds),
                       QStringLiteral("update")};
}

QStringList transientUnitArgs(const QString &tool, const QStringList &args, int runtimeSec)
{
    return QStringList{
        QStringLiteral("--quiet"),
        QStringLiteral("--wait"),
        QStringLiteral("--pipe"),
        QStringLiteral("--collect"),
        QStringLiteral("--description=dde-printer Canon 32-bit support"),
        QStringLiteral("-p"), QStringLiteral("RuntimeMaxSec=%1").arg(runtimeSec),
        QStringLiteral("-E"), QStringLiteral("DEBIAN_FRONTEND=noninteractive"),
        QStringLiteral("-E"), QStringLiteral("APT_LISTCHANGES_FRONTEND=none"),
        QStringLiteral("-E"), QStringLiteral("LANG=C"),
        QStringLiteral("-E"), QStringLiteral("PATH=/usr/sbin:/usr/bin:/sbin:/bin"),
        tool,
    } + args;
}

void setLockRetryDelayMs(int ms)
{
    g_lockRetryDelayMs = ms < 0 ? kLockRetryDelayMs : ms;
}

void run(QObject *owner, std::function<void(int)> done)
{
    readState(owner, [owner, done](bool architectureEnabled, bool librariesInstalled) {
        const auto ctx = std::make_shared<Context>();
        ctx->owner = owner;
        ctx->done = done;
        const Plan plan = makePlan(architectureEnabled, librariesInstalled);
        if (plan.addArchitecture)
            addArchitecture(ctx);
        else if (plan.installLibraries)
            updateLists(ctx);
        else
            startCcpd(ctx);
    });
}

}  // namespace CanonI386Repair
