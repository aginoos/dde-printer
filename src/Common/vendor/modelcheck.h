// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MODELCHECK_H
#define MODELCHECK_H

#include "modelfixmap.h"
#include "safeprocess.h"

#include <QFileDevice>
#include <QList>
#include <QString>

#include <functional>

// Evaluates the declarative checks of a model entry with the rights of the
// calling user: only read-only queries (systemctl is-active / is-enabled,
// dpkg-query, file permissions) and always through argument lists.
namespace ModelCheckRunner {

// Runs the read-only queries; replaceable so that tests never start systemctl
// or dpkg-query. Same arguments as SafeProcess::run.
using CommandRunner = std::function<SafeProcess::Result(const QString &, const QStringList &, int)>;

// Installs `runner` and returns the previous one; an empty function selects
// SafeProcess::run again (the default).
CommandRunner setCommandRunner(CommandRunner runner);

// Checks that do not pass. Blocking; each query is bounded by a timeout.
QList<ModelCheck> failedChecks(const QList<ModelCheck> &checks);

bool passes(const ModelCheck &check);

// `dpkg-query -W -f='${Status}'` output of an installed package.
bool isInstalledStatus(const QString &statusOutput);

// True when group or others may write.
bool hasUnsafeWrite(QFileDevice::Permissions permissions);

// Every file in `dir` matching `glob` is writable by its owner only; a
// dangling link counts as unsafe because its permissions cannot be judged.
bool filesAreSafe(const QString &dir, const QString &glob);

}  // namespace ModelCheckRunner

#endif  // MODELCHECK_H
