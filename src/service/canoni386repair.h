// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

// Canon CAPT drivers (ccpd, captstatusui) are 32-bit programs. This repair
// enables the i386 architecture when needed, refreshes the package lists,
// installs the fixed Canon library list and starts ccpd. apt and dpkg run in
// a transient systemd unit outside the service's sandbox (see
// transientUnitArgs()); only fixed tool names and arguments are ever used.
namespace CanonI386Repair {

constexpr int kLockRetries = 3;
constexpr int kLockRetryDelayMs = 10000;
constexpr int kUpdateTimeoutMs = 180000;
constexpr int kLockWaitSeconds = 60;

enum class Stage { AddArchitecture, Update, Install };

struct Plan {
    bool addArchitecture = false;
    bool installLibraries = false;
};

// What has to be done for the current state of the system.
Plan makePlan(bool architectureEnabled, bool librariesInstalled);

// Whether apt/dpkg output says another process holds the package lock.
bool isLockContention(const QString &output);

// Number of i386 packages that still have files or configuration, from `dpkg-query -W -f '${db:Status-Abbrev} ${Architecture}\n'`.
int countInstalledI386(const QString &queryOutput);

// Number of fully installed packages in `dpkg-query -W -f '${db:Status-Abbrev}\n'` output.
int countInstalledPackages(const QString &statusOutput);

// The architecture is removed again only if this run added it and nothing
// of it is installed.
bool shouldRollbackArchitecture(bool addedByThisRun, int installedI386Packages);

// RepairCode for a failed stage.
int codeForFailure(Stage stage, bool lockBusy);

// Arguments for dpkg-query that print the status of `packages`, one line each.
QStringList statusQueryArgs(const QStringList &packages);
// Arguments for the full i386 package count used by the rollback decision.
QStringList installedArchitectureQueryArgs();
QStringList aptUpdateArgs();

// systemd-run arguments that run `tool args...` as a one-shot unit and wait
// for it. The unit is killed by systemd after `runtimeSec` seconds.
QStringList transientUnitArgs(const QString &tool, const QStringList &args, int runtimeSec);

// Runs the whole repair; `done` gets a RepairCode exactly once.
void run(QObject *owner, std::function<void(int)> done);

}  // namespace CanonI386Repair
