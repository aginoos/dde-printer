// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TROUBLESHOOTDATA_H
#define TROUBLESHOOTDATA_H

#include <QByteArray>
#include <QString>

// Loading of the data files that drive the troubleshooting dialog. The files
// are installed by the package under a root-owned directory; anything that is
// not owned by the expected user, is a symbolic link or is writable by group
// or others is ignored, so a tampered file can never change what is checked.
namespace TroubleshootData {

constexpr qint64 kMaxFileSize = 256 * 1024;

// Directory the package installs the JSON files into.
QString dataDir();

// Regular file (no symlink) owned by `ownerId`, not writable by group/others.
bool isTrustedFile(const QString &path, uint ownerId = 0);

// Whole content of a trusted file, empty when it is missing, untrusted or too big.
QByteArray readTrustedFile(const QString &path, uint ownerId = 0);

}  // namespace TroubleshootData

#endif  // TROUBLESHOOTDATA_H
