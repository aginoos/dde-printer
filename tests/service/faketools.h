// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Test helpers for code that runs external tools through CommandRunner:
// FakeTools writes small shell scripts into a temporary directory, routes
// CommandRunner::findTool() to them and records every invocation, so a test
// can assert which commands ran, with which arguments, and in which order.
// waitForResult() runs an asynchronous operation to completion.

#include "commandrunner.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>
#include <optional>

class FakeTools
{
public:
    FakeTools()
    {
        m_valid = m_dir.isValid();
        CommandRunner::setToolResolver([this](const QString &name) { return pathOf(name); });
    }
    ~FakeTools() { CommandRunner::setToolResolver({}); }
    FakeTools(const FakeTools &) = delete;
    FakeTools &operator=(const FakeTools &) = delete;

    bool isValid() const { return m_valid; }
    // Directory that tests may use for state files read by script bodies.
    QString dir() const { return m_dir.path(); }

    // Installs tool `name`. `body` is /bin/sh code run after the invocation was logged.
    void add(const QString &name, const QString &body)
    {
        const QString path = m_dir.filePath("tool-" + name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        const QString script = QStringLiteral("#!/bin/sh\n"
                                              "{ printf '%s' '%1'; for a in \"$@\"; do printf '\\t%s' \"$a\"; done; printf '\\n'; } >> '%2'\n"
                                              "%3\n")
                                   .arg(name, logPath(), body);
        file.write(script.toUtf8());
        file.close();
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        m_tools.insert(name, path);
    }

    QString pathOf(const QString &name) const { return m_tools.value(name); }

    // One entry per invocation: the tool name followed by its arguments.
    QList<QStringList> calls() const
    {
        QList<QStringList> result;
        QFile file(logPath());
        if (!file.open(QIODevice::ReadOnly))
            return result;
        const QStringList lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines)
            result.append(line.split('\t'));
        return result;
    }

    QList<QStringList> callsOf(const QString &name) const
    {
        QList<QStringList> result;
        for (const QStringList &call : calls()) {
            if (call.first() == name)
                result.append(call.mid(1));
        }
        return result;
    }

    QStringList toolOrder() const
    {
        QStringList names;
        for (const QStringList &call : calls())
            names << call.first();
        return names;
    }

private:
    QString logPath() const { return m_dir.filePath("calls.log"); }

    QTemporaryDir m_dir;
    bool m_valid = false;
    QHash<QString, QString> m_tools;
};

// Result of waitForResult(): the value passed to the callback and how often it was called.
template <typename T>
struct Waited
{
    std::optional<T> value;
    int calls = 0;
};

// `start` receives the completion callback to hand to the operation under test.
// Waits (at most timeoutMs) for the first call, then a little longer so that a
// second call would be counted.
template <typename T>
Waited<T> waitForResult(const std::function<void(std::function<void(T)>)> &start, int timeoutMs = 20000)
{
    Waited<T> waited;
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    start([&waited, &loop](T result) {
        waited.value = result;
        ++waited.calls;
        loop.quit();
    });
    if (waited.calls == 0) {
        guard.start(timeoutMs);
        loop.exec();
    }
    QTest::qWait(150);
    return waited;
}
