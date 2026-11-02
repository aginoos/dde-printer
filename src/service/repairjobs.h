// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFileDevice>
#include <QObject>
#include <QString>

#include <functional>

// Repairs offered by the troubleshooting dialog. The client sends only an id;
// every id maps to a fixed sequence of actions compiled in here, so neither a
// data file nor the caller can choose a command, a path or an argument.
namespace RepairJobs {

constexpr int kStepTimeoutMs = 60000;

// Ids with an implementation: "cups_service", "canon_lbp2900", "brother_dcp_mfc".
bool isKnownFix(const QString &fixId);

// Runs the repair; `done` gets 0 on success and 1 on failure, exactly once.
void run(QObject *owner, const QString &fixId, std::function<void(int)> done);

// Permissions with the group and others write bits cleared.
QFileDevice::Permissions withoutGroupOtherWrite(QFileDevice::Permissions permissions);

// Regular, non-link files of the Brother CUPS filter directory that the repair may change.
bool isRepairableFilterName(const QString &fileName);

}  // namespace RepairJobs
