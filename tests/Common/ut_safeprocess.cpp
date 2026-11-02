// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>
#include <QTimer>

#include "safeprocess.h"

using namespace SafeProcess;

namespace {

QString repeated(char c, int count)
{
    return QString(count, QLatin1Char(c));
}

}  // namespace

// ---------------------------------------------------------------- findTool

TEST(ut_SafeProcess, findToolLooksUpBareNames)
{
    const QString sh = findTool("sh");
    EXPECT_FALSE(sh.isEmpty());
    EXPECT_TRUE(sh.startsWith('/'));
}

TEST(ut_SafeProcess, findToolAcceptsExecutableAbsolutePath)
{
    EXPECT_EQ(findTool("/bin/sh"), QString("/bin/sh"));
}

TEST(ut_SafeProcess, findToolRejectsEverythingElse)
{
    EXPECT_TRUE(findTool("").isEmpty());
    EXPECT_TRUE(findTool("no-such-tool-zzz").isEmpty());
    EXPECT_TRUE(findTool("/nonexistent/tool").isEmpty());
    EXPECT_TRUE(findTool("/etc/passwd").isEmpty());   // exists but is not executable
    EXPECT_TRUE(findTool("../bin/sh").isEmpty());     // never resolved relative to a directory
    EXPECT_TRUE(findTool("bin/sh").isEmpty());
}

// --------------------------------------------------------------------- run

TEST(ut_SafeProcess, runCapturesStdoutAndStderr)
{
    const Result res = run("sh", {"-c", "echo out; echo err 1>&2"});
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(res.status(), 0);
    EXPECT_EQ(res.out.trimmed(), QString("out"));
    EXPECT_EQ(res.err.trimmed(), QString("err"));
}

TEST(ut_SafeProcess, runReportsExitCode)
{
    const Result res = run("sh", {"-c", "exit 3"});
    EXPECT_TRUE(res.started);
    EXPECT_FALSE(res.timedOut);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.exitCode, 3);
    EXPECT_EQ(res.status(), -1);
}

TEST(ut_SafeProcess, runUnknownToolDoesNotStart)
{
    const Result res = run("no-such-tool-zzz", {});
    EXPECT_FALSE(res.started);
    EXPECT_FALSE(res.timedOut);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status(), -1);
}

TEST(ut_SafeProcess, runKillsOnTimeout)
{
    const Result res = run("sleep", {"5"}, 200);
    EXPECT_TRUE(res.started);
    EXPECT_TRUE(res.timedOut);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status(), -2);
}

TEST(ut_SafeProcess, runPassesArgumentsVerbatim)
{
    const QString tricky = "a b;c\nd $(id) `id` && *";
    const Result res = run("printf", {"%s", tricky});
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(res.out, tricky);
}

TEST(ut_SafeProcess, runUsesCleanEnvironment)
{
    qputenv("DDE_PRINTER_TEST_LEAK", "1");
    const Result res = run("env", {});
    qunsetenv("DDE_PRINTER_TEST_LEAK");
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(res.out.contains("PATH=/usr/sbin:/usr/bin:/sbin:/bin"));
    EXPECT_TRUE(res.out.contains("LC_ALL=C.UTF-8"));
    EXPECT_FALSE(res.out.contains("DDE_PRINTER_TEST_LEAK"));
    EXPECT_FALSE(res.out.contains("HOME="));
}

// --------------------------------------------------------- runKeepingUiAlive

TEST(ut_SafeProcess, runKeepingUiAliveCollectsOutput)
{
    const Result res = runKeepingUiAlive("sh", {"-c", "echo out; exit 0"});
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(res.out.trimmed(), QString("out"));
}

TEST(ut_SafeProcess, runKeepingUiAliveReportsExitCode)
{
    const Result res = runKeepingUiAlive("sh", {"-c", "exit 4"});
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.exitCode, 4);
}

TEST(ut_SafeProcess, runKeepingUiAliveTimesOut)
{
    const Result res = runKeepingUiAlive("sleep", {"5"}, 200);
    EXPECT_TRUE(res.timedOut);
    EXPECT_EQ(res.status(), -2);
}

TEST(ut_SafeProcess, runKeepingUiAliveUnknownTool)
{
    const Result res = runKeepingUiAlive("no-such-tool-zzz", {});
    EXPECT_FALSE(res.started);
    EXPECT_EQ(res.status(), -1);
}

TEST(ut_SafeProcess, runKeepingUiAliveRunsTheEventLoop)
{
    bool fired = false;
    QTimer::singleShot(50, [&fired]() { fired = true; });
    const Result res = runKeepingUiAlive("sleep", {"0.4"});
    EXPECT_TRUE(res.ok());
    EXPECT_TRUE(fired);
}

// -------------------------------------------------------------- validators

TEST(ut_SafeProcess, packageNameAccepts)
{
    EXPECT_TRUE(isValidPackageName("ab"));
    EXPECT_TRUE(isValidPackageName("g++"));
    EXPECT_TRUE(isValidPackageName("com.hp.hplip"));
    EXPECT_TRUE(isValidPackageName("printer-driver-gutenprint"));
    EXPECT_TRUE(isValidPackageName("0ad"));
    EXPECT_TRUE(isValidPackageName(repeated('a', 128)));
}

TEST(ut_SafeProcess, packageNameRejects)
{
    EXPECT_FALSE(isValidPackageName(""));
    EXPECT_FALSE(isValidPackageName("a"));
    EXPECT_FALSE(isValidPackageName(repeated('a', 129)));
    EXPECT_FALSE(isValidPackageName("Foo"));
    EXPECT_FALSE(isValidPackageName("-oops"));
    EXPECT_FALSE(isValidPackageName("+oops"));
    EXPECT_FALSE(isValidPackageName("foo bar"));
    EXPECT_FALSE(isValidPackageName("a; rm -rf /"));
    EXPECT_FALSE(isValidPackageName("foo/../bar"));
    EXPECT_FALSE(isValidPackageName("foo:amd64"));
    EXPECT_FALSE(isValidPackageName("foo*"));
    EXPECT_FALSE(isValidPackageName("foo\n"));
    EXPECT_FALSE(isValidPackageName("\nfoo"));
    EXPECT_FALSE(isValidPackageName(QString::fromUtf8("pkg-\xe6\x89\x93\xe5\x8d\xb0")));
}

TEST(ut_SafeProcess, packageVersionAccepts)
{
    EXPECT_TRUE(isValidPackageVersion("1.0"));
    EXPECT_TRUE(isValidPackageVersion("1:3.18.12-2+b1~bpo13"));
    EXPECT_TRUE(isValidPackageVersion("2.0.0_beta"));
    EXPECT_TRUE(isValidPackageVersion(repeated('1', 128)));
}

TEST(ut_SafeProcess, packageVersionRejects)
{
    EXPECT_FALSE(isValidPackageVersion(""));
    EXPECT_FALSE(isValidPackageVersion(repeated('1', 129)));
    EXPECT_FALSE(isValidPackageVersion("-1"));
    EXPECT_FALSE(isValidPackageVersion("~1"));
    EXPECT_FALSE(isValidPackageVersion("1 2"));
    EXPECT_FALSE(isValidPackageVersion("1;id"));
    EXPECT_FALSE(isValidPackageVersion("1\n"));
    EXPECT_FALSE(isValidPackageVersion("1/2"));
}

TEST(ut_SafeProcess, hostAccepts)
{
    EXPECT_TRUE(isValidHost("a"));
    EXPECT_TRUE(isValidHost("printer-1.local"));
    EXPECT_TRUE(isValidHost("PRINTER.Example.COM"));
    EXPECT_TRUE(isValidHost("192.168.1.8"));
    EXPECT_TRUE(isValidHost("fe80::1"));
    EXPECT_TRUE(isValidHost("::1"));
    EXPECT_TRUE(isValidHost("2001:db8::1"));
    EXPECT_TRUE(isValidHost(repeated('a', 63)));
}

TEST(ut_SafeProcess, hostRejects)
{
    EXPECT_FALSE(isValidHost(""));
    EXPECT_FALSE(isValidHost("-c"));
    EXPECT_FALSE(isValidHost("--help"));
    EXPECT_FALSE(isValidHost("a b"));
    EXPECT_FALSE(isValidHost("a;b"));
    EXPECT_FALSE(isValidHost("$(id)"));
    EXPECT_FALSE(isValidHost("host\n"));
    EXPECT_FALSE(isValidHost("host."));
    EXPECT_FALSE(isValidHost(".host"));
    EXPECT_FALSE(isValidHost("a..b"));
    EXPECT_FALSE(isValidHost("-a.example"));
    EXPECT_FALSE(isValidHost("a-.example"));
    EXPECT_FALSE(isValidHost("[::1]"));
    EXPECT_FALSE(isValidHost(repeated('a', 64)));
    EXPECT_FALSE(isValidHost((repeated('a', 63) + ".").repeated(3) + repeated('a', 62)));   // 254 characters
}

TEST(ut_SafeProcess, deviceTokenAccepts)
{
    EXPECT_TRUE(isValidDeviceToken("CF6L088530"));
    EXPECT_TRUE(isValidDeviceToken("a-b_c"));
    EXPECT_TRUE(isValidDeviceToken(repeated('Z', 64)));
}

TEST(ut_SafeProcess, deviceTokenRejects)
{
    EXPECT_FALSE(isValidDeviceToken(""));
    EXPECT_FALSE(isValidDeviceToken(repeated('Z', 65)));
    EXPECT_FALSE(isValidDeviceToken("../x"));
    EXPECT_FALSE(isValidDeviceToken("a b"));
    EXPECT_FALSE(isValidDeviceToken("a.b"));
    EXPECT_FALSE(isValidDeviceToken("a\n"));
    EXPECT_FALSE(isValidDeviceToken(QString::fromUtf8("\xe5\xba\x8f\xe5\x88\x97")));
}

TEST(ut_SafeProcess, queueNameAccepts)
{
    EXPECT_TRUE(isValidQueueName("HP_LaserJet-1.0+"));
    EXPECT_TRUE(isValidQueueName("_underscore"));
    EXPECT_TRUE(isValidQueueName("x"));
    EXPECT_TRUE(isValidQueueName(repeated('q', 127)));
}

TEST(ut_SafeProcess, queueNameRejects)
{
    EXPECT_FALSE(isValidQueueName(""));
    EXPECT_FALSE(isValidQueueName(repeated('q', 128)));
    EXPECT_FALSE(isValidQueueName("-x"));
    EXPECT_FALSE(isValidQueueName("--x"));
    EXPECT_FALSE(isValidQueueName(".x"));
    EXPECT_FALSE(isValidQueueName("a b"));
    EXPECT_FALSE(isValidQueueName("a/b"));
    EXPECT_FALSE(isValidQueueName("a#b"));
    EXPECT_FALSE(isValidQueueName("a*"));
    EXPECT_FALSE(isValidQueueName("a\n"));
    EXPECT_FALSE(isValidQueueName(QString::fromUtf8("\xe6\x89\x93\xe5\x8d\xb0\xe6\x9c\xba")));
}

TEST(ut_SafeProcess, safeAbsolutePathAccepts)
{
    EXPECT_TRUE(isSafeAbsolutePath("/"));
    EXPECT_TRUE(isSafeAbsolutePath("/usr/share/cups/model/a.ppd"));
    EXPECT_TRUE(isSafeAbsolutePath("/a/..b"));          // not a ".." component
    EXPECT_TRUE(isSafeAbsolutePath("/a/./b"));
    EXPECT_TRUE(isSafeAbsolutePath("/opt/with space/x"));
    EXPECT_TRUE(isSafeAbsolutePath("/" + repeated('a', 4095)));
}

TEST(ut_SafeProcess, safeAbsolutePathRejects)
{
    EXPECT_FALSE(isSafeAbsolutePath(""));
    EXPECT_FALSE(isSafeAbsolutePath("relative/path"));
    EXPECT_FALSE(isSafeAbsolutePath("-rf"));
    EXPECT_FALSE(isSafeAbsolutePath("/tmp/../etc/passwd"));
    EXPECT_FALSE(isSafeAbsolutePath("/.."));
    EXPECT_FALSE(isSafeAbsolutePath("/a/b/.."));
    EXPECT_FALSE(isSafeAbsolutePath("/a\nb"));
    EXPECT_FALSE(isSafeAbsolutePath(QString::fromLatin1("/a\x01" "b")));
    EXPECT_FALSE(isSafeAbsolutePath(QString::fromLatin1("/a\x7f" "b")));
    EXPECT_FALSE(isSafeAbsolutePath(QString::fromLatin1("/a\0b", 4)));
    EXPECT_FALSE(isSafeAbsolutePath("/" + repeated('a', 4096)));
}
