// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autostartguard.h"

#include <QByteArray>
#include <QFile>
#include <QStringList>

#include <sys/stat.h>

namespace {

const QStringList &allowedDirs()
{
    static const QStringList dirs = {
        "/opt/printer-drivers/com.pantum.pantum/config/",
        "/opt/printer-drivers/com.lanxum-ga-series/config/",
        "/opt/printer-drivers/com.hp.hplip/config/",
    };
    return dirs;
}

const char kScriptName[] = "autostart.sh";

bool isTrusted(const QString &path, mode_t type)
{
    struct stat st {};
    // lstat: a symbolic link is reported as such and fails the type test
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0)
        return false;
    return (st.st_mode & S_IFMT) == type && st.st_uid == 0
        && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

bool isTrustedDirChain(const QString &dirPath)
{
    const QStringList parts = dirPath.split('/', Qt::SkipEmptyParts);
    QString current;
    for (const QString &part : parts) {
        current += "/" + part;
        if (!isTrusted(current, S_IFDIR))
            return false;
    }
    return !parts.isEmpty();
}

}

namespace AutostartGuard {

QString resolveScript(const QString &dirPath)
{
    if (!allowedDirs().contains(dirPath) || !isTrustedDirChain(dirPath))
        return QString();

    const QString script = dirPath + kScriptName;
    struct stat st {};
    if (!isTrusted(script, S_IFREG)
        || ::lstat(QFile::encodeName(script).constData(), &st) != 0
        || (st.st_mode & S_IXUSR) == 0)
        return QString();
    return script;
}

}
