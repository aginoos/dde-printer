// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QEventLoop>
#include <QObject>
#include <QTest>
#include <QTimer>

#include "commandrunner.h"

#include <cerrno>
#include <signal.h>

namespace {

struct Outcome
{
    CommandResult result;
    int calls = 0;
};

// Run a command and spin the event loop until the callback fired (or a
// safety timer expired), then keep spinning briefly so a second call to the
// callback would show up in `calls`.
Outcome runAndWait(const QString &program, const QStringList &args, int timeoutMs = 10000)
{
    Outcome outcome;
    QObject owner;
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);

    CommandRunner::run(&owner, program, args, [&outcome, &loop](const CommandResult &result) {
        outcome.result = result;
        ++outcome.calls;
        loop.quit();
    }, timeoutMs);

    if (outcome.calls == 0) {
        guard.start(15000);
        loop.exec();
    }
    QTest::qWait(150);
    return outcome;
}

QString tool(const char *name)
{
    return CommandRunner::findTool(QString::fromLatin1(name));
}

}  // namespace

TEST(ut_CommandRunner, findToolLooksInSystemDirectories)
{
    const QString sh = CommandRunner::findTool("sh");
    EXPECT_FALSE(sh.isEmpty());
    EXPECT_TRUE(sh.startsWith('/'));
    EXPECT_TRUE(CommandRunner::findTool("no-such-tool-zzz").isEmpty());
    EXPECT_TRUE(CommandRunner::findTool("").isEmpty());
    EXPECT_TRUE(CommandRunner::findTool("/bin/sh").isEmpty());   // names only, absolute paths are not tools
}

TEST(ut_CommandRunner, emptyProgramReportsFailureImmediately)
{
    int calls = 0;
    CommandResult seen;
    seen.started = true;
    QObject owner;
    CommandRunner::run(&owner, QString(), {}, [&](const CommandResult &result) {
        seen = result;
        ++calls;
    });
    EXPECT_EQ(calls, 1);   // synchronous, no event loop needed
    EXPECT_FALSE(seen.started);
    EXPECT_FALSE(seen.ok());
}

TEST(ut_CommandRunner, successCapturesOutputAndCallsBackOnce)
{
    const Outcome outcome = runAndWait(tool("echo"), {"hello"});
    EXPECT_EQ(outcome.calls, 1);
    EXPECT_TRUE(outcome.result.started);
    EXPECT_FALSE(outcome.result.timedOut);
    EXPECT_EQ(outcome.result.exitCode, 0);
    EXPECT_TRUE(outcome.result.ok());
    EXPECT_EQ(outcome.result.output, QByteArray("hello\n"));
}

TEST(ut_CommandRunner, mergesStdoutAndStderr)
{
    const Outcome outcome = runAndWait(tool("sh"), {"-c", "echo out; echo err 1>&2"});
    ASSERT_EQ(outcome.calls, 1);
    EXPECT_TRUE(outcome.result.output.contains("out"));
    EXPECT_TRUE(outcome.result.output.contains("err"));
}

TEST(ut_CommandRunner, reportsExitCode)
{
    const Outcome outcome = runAndWait(tool("sh"), {"-c", "exit 5"});
    EXPECT_EQ(outcome.calls, 1);
    EXPECT_TRUE(outcome.result.started);
    EXPECT_EQ(outcome.result.exitCode, 5);
    EXPECT_FALSE(outcome.result.ok());
}

TEST(ut_CommandRunner, programThatCannotStart)
{
    const Outcome outcome = runAndWait("/nonexistent/dir/program", {});
    EXPECT_EQ(outcome.calls, 1);
    EXPECT_FALSE(outcome.result.started);
    EXPECT_FALSE(outcome.result.ok());
    EXPECT_EQ(outcome.result.exitCode, -1);
}

TEST(ut_CommandRunner, passesArgumentsVerbatim)
{
    const QString tricky = "a b;c\nd $(id) `id` && *";
    const Outcome outcome = runAndWait(tool("printf"), {"%s", tricky});
    ASSERT_EQ(outcome.calls, 1);
    EXPECT_EQ(QString::fromUtf8(outcome.result.output), tricky);
}

TEST(ut_CommandRunner, runsWithFixedEnvironmentAndRootDirectory)
{
    qputenv("DDE_PRINTER_TEST_LEAK", "1");
    const Outcome env = runAndWait(tool("env"), {});
    qunsetenv("DDE_PRINTER_TEST_LEAK");
    ASSERT_EQ(env.calls, 1);
    EXPECT_TRUE(env.result.output.contains("PATH=/usr/sbin:/usr/bin:/sbin:/bin"));
    EXPECT_TRUE(env.result.output.contains("LC_ALL=C"));
    EXPECT_FALSE(env.result.output.contains("DDE_PRINTER_TEST_LEAK"));
    EXPECT_FALSE(env.result.output.contains("HOME="));

    const Outcome pwd = runAndWait(tool("pwd"), {});
    ASSERT_EQ(pwd.calls, 1);
    EXPECT_EQ(pwd.result.output, QByteArray("/\n"));
}

TEST(ut_CommandRunner, killsAfterTimeoutAndCallsBackOnce)
{
    const Outcome outcome = runAndWait(tool("sleep"), {"10"}, 300);
    // the callback must fire exactly once, and a killed command is never ok()
    EXPECT_EQ(outcome.calls, 1);
    EXPECT_TRUE(outcome.result.started);
    EXPECT_FALSE(outcome.result.ok());
    EXPECT_EQ(outcome.result.exitCode, -1);
}

TEST(ut_CommandRunner, commandResultOkRequiresStartedInTimeAndZeroExit)
{
    CommandResult result;
    EXPECT_FALSE(result.ok());
    result.started = true;
    result.exitCode = 0;
    EXPECT_TRUE(result.ok());
    result.timedOut = true;
    EXPECT_FALSE(result.ok());
    result.timedOut = false;
    result.exitCode = 1;
    EXPECT_FALSE(result.ok());
}

TEST(ut_CommandRunner, timeoutIsReportedAsTimedOut)
{
    const Outcome outcome = runAndWait(tool("sleep"), {"10"}, 300);
    ASSERT_EQ(outcome.calls, 1);
    EXPECT_TRUE(outcome.result.timedOut);
}

TEST(ut_CommandRunner, timeoutStopsTheWholeProcessGroup)
{
    // The shell starts a background sleep and prints its pid; both must die.
    const Outcome outcome = runAndWait(tool("sh"), {"-c", "sleep 30 & echo $!; wait"}, 500);
    ASSERT_EQ(outcome.calls, 1);
    EXPECT_TRUE(outcome.result.timedOut);
    bool ok = false;
    const int childPid = QString::fromLatin1(outcome.result.output).trimmed().toInt(&ok);
    ASSERT_TRUE(ok);
    ASSERT_GT(childPid, 1);
    QTest::qWait(400);
    EXPECT_EQ(::kill(childPid, 0), -1);
    EXPECT_EQ(errno, ESRCH);
}

TEST(ut_CommandRunner, appendCappedStopsAtTheLimit)
{
    QByteArray buffer;
    CommandRunner::appendCapped(&buffer, "abcd", 6);
    EXPECT_EQ(buffer, QByteArray("abcd"));
    CommandRunner::appendCapped(&buffer, "efgh", 6);
    EXPECT_EQ(buffer, QByteArray("abcdef"));
    CommandRunner::appendCapped(&buffer, "ij", 6);
    EXPECT_EQ(buffer, QByteArray("abcdef"));
    CommandRunner::appendCapped(&buffer, QByteArray(), 100);
    EXPECT_EQ(buffer.size(), 6);
}

TEST(ut_CommandRunner, outputIsCapped)
{
    // Roughly 2 MiB of output must be cut at the limit.
    const Outcome outcome = runAndWait(tool("sh"), {"-c", "head -c 2097152 /dev/zero"}, 20000);
    ASSERT_EQ(outcome.calls, 1);
    EXPECT_EQ(outcome.result.output.size(), CommandRunner::kMaxOutputBytes);
}

TEST(ut_CommandRunner, childRunsInItsOwnSession)
{
    const Outcome outcome = runAndWait(tool("sh"), {"-c", "ps -o sid= -p $$ | tr -d ' '; echo $$"});
    ASSERT_EQ(outcome.calls, 1);
    const QStringList lines = QString::fromLatin1(outcome.result.output).split('\n', Qt::SkipEmptyParts);
    if (lines.size() == 2)   // ps may be missing in minimal containers
        EXPECT_EQ(lines.at(0), lines.at(1));
}
