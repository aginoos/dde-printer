// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autostartguard.h"

#include <QByteArray>
#include <QFile>
#include <QStringList>

#include <sys/stat.h>

namespace {

// Knobs for the unit tests (which #include this file). The defaults are the
// production policy: fixed vendor directories, root as the only trusted owner,
// every component of the path checked.
struct TestPolicy {
    QStringList dirs;       // replaces the vendor directories when not empty
    uid_t owner = 0;        // the uid that counts as trusted
    QString chainBase;      // when set, only this path and its children are checked
};
TestPolicy g_policy;

const QStringList &allowedDirs()
{
    if (!g_policy.dirs.isEmpty())
        return g_policy.dirs;
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
    return (st.st_mode & S_IFMT) == type && st.st_uid == g_policy.owner
        && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

bool isTrustedDirChain(const QString &dirPath)
{
    const QStringList parts = dirPath.split('/', Qt::SkipEmptyParts);
    QString current;
    for (const QString &part : parts) {
        current += "/" + part;
        const bool inScope = g_policy.chainBase.isEmpty() || current == g_policy.chainBase
            || current.startsWith(g_policy.chainBase + "/");
        if (inScope && !isTrusted(current, S_IFDIR))
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
