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

// Run program with an argument list. `done` is invoked exactly once, on the
// thread of `owner`. The process is killed when the timeout expires.
void run(QObject *owner, const QString &program, const QStringList &args,
         Callback done, int timeoutMs = kDefaultTimeoutMs);

// Resolve a tool in the fixed system directories; empty when not installed.
QString findTool(const QString &name);

}
