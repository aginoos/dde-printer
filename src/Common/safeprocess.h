// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SAFEPROCESS_H
#define SAFEPROCESS_H

#include <QString>
#include <QStringList>
#include <QByteArray>

// Helpers for running external tools without a shell, plus whitelist
// validators for values that come from the network, devices or PPD files.
namespace SafeProcess {

struct Result
{
    bool started = false;   // the program could be started
    bool timedOut = false;  // killed after the timeout elapsed
    int exitCode = -1;
    QString out;            // stdout
    QString err;            // stderr

    bool ok() const { return started && !timedOut && exitCode == 0; }
    // 0 on success, -2 on timeout, -1 on any other failure.
    int status() const { return ok() ? 0 : (timedOut ? -2 : -1); }
};

// Run `program` (bare name is looked up in fixed system directories, never in
// the caller's PATH) with an argument list. Blocks the calling thread.
Result run(const QString &program, const QStringList &args, int timeoutMs = 30000);

// Same, but keeps the calling thread's event loop alive while waiting, with
// user input events excluded. Use it from the UI thread instead of
// waitForFinished(), which freezes the window.
Result runKeepingUiAlive(const QString &program, const QStringList &args, int timeoutMs = 30000);

// Absolute path of a system tool, or an empty string if it is not installed.
QString findTool(const QString &name);

// Debian package name: [a-z0-9][a-z0-9+.-]+, 2..128 chars.
bool isValidPackageName(const QString &name);
// Debian version string as accepted by dpkg (no whitespace, no leading '-').
bool isValidPackageVersion(const QString &version);
// Host name (RFC 1123 labels) or IPv4/IPv6 literal; never starts with '-'.
bool isValidHost(const QString &host);
// Device-provided token (serial number etc.): [A-Za-z0-9_-]{1,64}.
bool isValidDeviceToken(const QString &token);
// CUPS queue name as accepted by the manager service.
bool isValidQueueName(const QString &name);
// Absolute file path without control characters, ".." components or a leading '-'.
bool isSafeAbsolutePath(const QString &path);

}  // namespace SafeProcess

#endif  // SAFEPROCESS_H
