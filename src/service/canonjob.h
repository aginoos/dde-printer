// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "inputvalidator.h"

#include <QObject>

#include <functional>

// Registration and removal of Canon CAPT printers: the work formerly done by
// the canonadd / canonremove shell scripts, now run as argument lists with no
// shell involved. Each job is fully asynchronous.
namespace CanonJob {

constexpr int kOk = 0;
constexpr int kFailed = 1;
constexpr int kAlreadyInstalled = 2;

using Done = std::function<void(int exitCode)>;

void install(QObject *owner, const CanonInstallRequest &request, Done done);
void remove(QObject *owner, const QString &printerName, Done done);

}
