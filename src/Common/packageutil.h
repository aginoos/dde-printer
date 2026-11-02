// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PACKAGEUTIL_H
#define PACKAGEUTIL_H

#include <QString>

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

// True when a lastore daemon (new or legacy name) is running or activatable.
bool lastoreAvailable();

// Parse `apt-cache policy` output; exposed for reuse and testing.
bool policyOffers(const QString &policyOutput, const QString &version);

}  // namespace PackageUtil

#endif  // PACKAGEUTIL_H
