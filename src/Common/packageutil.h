// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PACKAGEUTIL_H
#define PACKAGEUTIL_H

#include <QString>
#include <QStringList>

// Read-only package queries built on SafeProcess (no shell, validated input).
namespace PackageUtil {

// Installed version of `package`, or an empty string when it is not installed
// (or the name is not a valid package name).
QString installedVersion(const QString &package);

bool isInstalled(const QString &package);

enum class VersionOrder { Older, Equal, Newer, Unknown };

// Compare two Debian versions with dpkg; Unknown when either is invalid.
VersionOrder compareVersions(const QString &lhs, const QString &rhs);

// True when the apt index offers `package` (exactly `version` when not empty).
// Never blocks the UI event loop.
bool isInstallable(const QString &package, const QString &version);

// Bus name of the lastore daemon (new or legacy) that is running or
// activatable, or an empty string on systems without lastore.
QString lastoreService();
bool lastoreAvailable();

// Names a printer driver install may touch. Package names that come from the
// driver server or from PPD files are only installed as root when they follow
// the naming of printer driver packages (Debian printer-driver-*, HPLIP,
// Canon, Epson, deepin vendor packages "com.<vendor>.<product>", ...).
// Anything else is refused, whatever the server says.
bool isAllowedDriverPackage(const QString &package);
// Adds names to the accepted set; only unit tests that need a package that is
// really installed on the build machine call this. Empty list resets.
void setExtraAllowedDriverPackages(const QStringList &names);

// Parse `apt-cache policy` output; exposed for reuse and testing.
bool policyOffers(const QString &policyOutput, const QString &version);

}  // namespace PackageUtil

#endif  // PACKAGEUTIL_H
