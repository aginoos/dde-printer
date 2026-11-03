// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFileDevice>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

// Repairs offered by the troubleshooting dialog. The client sends only an id;
// every id maps to a fixed sequence of actions compiled in here, so neither a
// data file nor the caller can choose a command, a path or an argument.
namespace RepairJobs {

constexpr int kStepTimeoutMs = 60000;
constexpr int kInstallTimeoutMs = 300000;

// Ids with an implementation: "cups_service", "canon_lbp2900", "brother_dcp_mfc".
bool isKnownFix(const QString &fixId);

// Runs the repair; `done` gets 0 on success and 1 on failure, exactly once.
void run(QObject *owner, const QString &fixId, std::function<void(int)> done);

// Fixed package list of the Canon LBP2900 repair; never taken from the caller.
QStringList canonI386Packages();

// Arguments for `dpkg`, and whether its output lists `arch` (e.g. "i386").
QStringList foreignArchitectureArgs();
bool hasForeignArchitecture(const QString &dpkgOutput, const QString &arch);

// Arguments for a non-interactive `apt-get install` of exactly `packages`.
QStringList aptInstallArgs(const QStringList &packages);

// Permissions with the group and others write bits cleared.
QFileDevice::Permissions withoutGroupOtherWrite(QFileDevice::Permissions permissions);

// Regular, non-link files of the Brother CUPS filter directory that the repair may change.
bool isRepairableFilterName(const QString &fileName);

}  // namespace RepairJobs
