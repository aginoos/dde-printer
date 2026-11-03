// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "vendor/troubleshootservice.h"

#include <QString>
#include <QStringList>

// Client side of the Canon CAPT 32-bit support: finds out whether the i386
// libraries the Canon driver needs are present, decides when the user has to
// confirm enabling them, and provides the texts. The privileged work is done
// by the root service (RunRepair "canon_lbp2900").
namespace CanonI386 {

struct Status {
    bool architectureEnabled = false;
    bool librariesInstalled = false;
};

// Id of the service repair that enables i386 and installs the libraries.
QString fixId();
// The fixed library list (the service uses the same names).
QStringList requiredLibraries();

// Builds the status from `dpkg --print-foreign-architectures` and from
// `dpkg-query -W -f '${db:Status-Abbrev}\n' <libraries>` output.
Status parseStatus(const QString &foreignArchitectures, const QString &libraryStatus, int expectedLibraries);
bool isReady(const Status &status);

// Reads the status with read-only commands. Blocks for a moment.
Status queryStatus();

// A repair button of this fix id needs the confirmation first when the system is not ready.
bool needsConfirmation(const QString &repairFixId, const Status &status);
// Adding a Canon CAPT printer needs it whenever the system is not ready.
bool needsConfirmationForDriver(const Status &status);

// Texts of the confirmation dialog and of a finished repair. outcomeText()
// is empty for Success, Denied and outcomes that are not Canon specific.
QString confirmTitle();
QString confirmMessage();
QString confirmAcceptText();
QString confirmCancelText();
QString cancelledText();
QString outcomeText(TroubleshootService::Outcome outcome);

}  // namespace CanonI386
