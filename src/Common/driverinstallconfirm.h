// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DRIVERINSTALLCONFIRM_H
#define DRIVERINSTALLCONFIRM_H

#include <QString>
#include <QStringList>

#include <functional>

// The fallback installer asks polkit through a generic "pkexec apt-get", whose
// authentication dialog cannot say what is about to be installed. This asks
// the user first, in dde-printer's own dialog, with the package names and
// versions that will be handed to apt.
namespace DriverInstallConfirm {

using Handler = std::function<bool(const QStringList &descriptions)>;

// "name (version)", or just "name" when the version is empty.
QString describe(const QString &package, const QString &version);

// Replaces the dialog (tests); an empty function restores the default.
void setHandler(Handler handler);

// Test seam: replaces the check "is there a widget application to show the dialog"; an empty function
// restores the real check.
void setGuiAvailableForTest(std::function<bool()> available);

// True when the user agreed. Without a GUI application nobody can agree, so
// the default answer is no.
bool confirm(const QStringList &descriptions);

}  // namespace DriverInstallConfirm

#endif  // DRIVERINSTALLCONFIRM_H
