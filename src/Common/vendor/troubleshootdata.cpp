// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "troubleshootdata.h"

#include <QFile>
#include <QFileInfo>

namespace TroubleshootData {

QString dataDir()
{
    return QStringLiteral("/usr/share/dde-printer/troubleshoot");
}

bool isTrustedFile(const QString &path, uint ownerId)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink())
        return false;
    if (info.ownerId() != ownerId)
        return false;
    return !(info.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther));
}

QByteArray readTrustedFile(const QString &path, uint ownerId)
{
    if (!isTrustedFile(path, ownerId) || QFileInfo(path).size() > kMaxFileSize)
        return QByteArray();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.read(kMaxFileSize);
}

}  // namespace TroubleshootData
