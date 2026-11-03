// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

struct CommandResult {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QByteArray output; // stdout and stderr merged

    bool ok() const { return started && !timedOut && exitCode == 0; }
};

// Asynchronous, shell-free execution of external tools with a fixed
// environment. Every call owns its own QProcess; nothing is shared between
// requests and the event loop is never blocked.
namespace CommandRunner {

using Callback = std::function<void(const CommandResult &)>;

constexpr int kDefaultTimeoutMs = 60000;
// Time between SIGTERM and SIGKILL for the process group of a timed out command.
constexpr int kTerminateGraceMs = 2000;
// Output beyond this size is dropped.
constexpr int kMaxOutputBytes = 1024 * 1024;

// Appends as much of `data` as fits below `maxBytes` in total.
void appendCapped(QByteArray *buffer, const QByteArray &data, int maxBytes);

// Run program with an argument list. `done` is invoked exactly once, on the
// thread of `owner`. When the timeout expires the whole process group is terminated
// (SIGTERM, then SIGKILL) without blocking, and `timedOut` is set.
void run(QObject *owner, const QString &program, const QStringList &args,
         Callback done, int timeoutMs = kDefaultTimeoutMs);

// Resolve a tool in the fixed system directories; empty when not installed.
QString findTool(const QString &name);

// Replaces the lookup done by findTool() (tests point it at fake tools in a
// temporary directory). An empty function restores the fixed system directories.
void setToolResolver(std::function<QString(const QString &)> resolver);

}
