// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "../service/faketools.h"
#include "ippusbblacklist.h"

using namespace IppUsbBlacklist;

namespace {

const char kStatus[] =
    "ipp-usb daemon: running\n"
    "ipp-usb devices:\n"
    " Num  Device              Vndr:Prod  Model\n"
    "   1. Bus 001 Device 018  232b:0ef5  \"Pantum P3301DN series\"\n"
    "      status: OK\n"
    "   2. Bus 001 Device 019  04B8:0E1A  \"EPSON ET-2820 Series\"\n"
    "      status: Failed\n";

}

TEST(ut_IppUsbBlacklist, normalizeVendorProduct)
{
    EXPECT_EQ(normalizeVendorProduct("04B8:0E1a"), std::optional<QString>("04b8:0e1a"));
    EXPECT_FALSE(normalizeVendorProduct("04b8:0e1"));
    EXPECT_FALSE(normalizeVendorProduct("04b8-0e1a"));
    EXPECT_FALSE(normalizeVendorProduct("04b8:0e1a\n"));
    EXPECT_FALSE(normalizeVendorProduct(" 04b8:0e1a"));
    EXPECT_FALSE(normalizeVendorProduct("g4b8:0e1a"));
    EXPECT_FALSE(normalizeVendorProduct(""));
}

TEST(ut_IppUsbBlacklist, isValidModel)
{
    EXPECT_TRUE(isValidModel("Pantum P3301DN series"));
    EXPECT_TRUE(isValidModel("HP LaserJet Pro M404-M405"));
    EXPECT_FALSE(isValidModel(""));
    EXPECT_FALSE(isValidModel(" padded "));
    EXPECT_FALSE(isValidModel("evil]\nblacklist = false"));
    EXPECT_FALSE(isValidModel("evil[x"));
    EXPECT_FALSE(isValidModel("a;b"));
    EXPECT_FALSE(isValidModel("HP*"));
    EXPECT_FALSE(isValidModel("a?b"));
    EXPECT_FALSE(isValidModel("a\\b"));
    EXPECT_FALSE(isValidModel(QString(129, 'a')));
    EXPECT_TRUE(isValidModel(QString(128, 'a')));
}

TEST(ut_IppUsbBlacklist, modelFromStatus)
{
    EXPECT_EQ(modelFromStatus(kStatus, "232b:0ef5"), std::optional<QString>("Pantum P3301DN series"));
    EXPECT_EQ(modelFromStatus(kStatus, "04b8:0e1a"), std::optional<QString>("EPSON ET-2820 Series"));
    EXPECT_FALSE(modelFromStatus(kStatus, "ffff:0000"));
    EXPECT_FALSE(modelFromStatus("ipp-usb devices: not found\n", "232b:0ef5"));
}

TEST(ut_IppUsbBlacklist, addEntryWritesMarkerSectionAndKey)
{
    const QString content = withEntry(QString(), "232b:0ef5", "Pantum P3301DN series");
    EXPECT_TRUE(content.contains("; dde-printer ippusb 232b:0ef5\n"));
    EXPECT_TRUE(content.contains("[Pantum P3301DN series]\n"));
    EXPECT_TRUE(content.contains("blacklist = true"));
    EXPECT_TRUE(hasEntry(content, "232b:0ef5"));
    EXPECT_FALSE(hasEntry(content, "04b8:0e1a"));
    EXPECT_EQ(entryModel(content, "232b:0ef5"), QString("Pantum P3301DN series"));
}

TEST(ut_IppUsbBlacklist, addIsIdempotent)
{
    const QString once = withEntry(QString(), "232b:0ef5", "A");
    EXPECT_EQ(withEntry(once, "232b:0ef5", "A"), once);
}

TEST(ut_IppUsbBlacklist, removeTouchesOnlyItsOwnBlock)
{
    QString content = withEntry(QString(), "232b:0ef5", "A");
    content = withEntry(content, "04b8:0e1a", "B");
    content = withEntry(content, "1111:2222", "C");

    const QString without = withoutEntry(content, "04b8:0e1a");
    EXPECT_FALSE(hasEntry(without, "04b8:0e1a"));
    EXPECT_FALSE(without.contains("[B]"));
    EXPECT_TRUE(hasEntry(without, "232b:0ef5"));
    EXPECT_TRUE(hasEntry(without, "1111:2222"));
    EXPECT_TRUE(without.contains("[A]"));
    EXPECT_TRUE(without.contains("[C]"));
}

TEST(ut_IppUsbBlacklist, removeLastAndOnlyEntry)
{
    const QString content = withEntry(QString(), "232b:0ef5", "A");
    EXPECT_EQ(withoutEntry(content, "232b:0ef5"), QString());
    const QString two = withEntry(content, "04b8:0e1a", "B");
    const QString left = withoutEntry(two, "04b8:0e1a");
    EXPECT_EQ(left, content);
}

TEST(ut_IppUsbBlacklist, removeUnknownLeavesContentAlone)
{
    const QString content = withEntry(QString(), "232b:0ef5", "A");
    EXPECT_EQ(withoutEntry(content, "ffff:0000"), content);
    EXPECT_EQ(entryModel(content, "ffff:0000"), QString());
}

TEST(ut_IppUsbBlacklist, readMissingFileIsEmptyAndWriteRoundTrips)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("quirks/dde-printer-blacklist.conf");

    const auto missing = readQuirks(path);
    ASSERT_TRUE(missing.has_value());
    EXPECT_TRUE(missing->isEmpty());

    const QString content = withEntry(QString(), "232b:0ef5", "Pantum P3301DN series");
    ASSERT_TRUE(writeQuirks(path, content));
    EXPECT_EQ(readQuirks(path), std::optional<QString>(content));

    const QFileInfo info(path);
    EXPECT_TRUE(info.permission(QFile::ReadOther));
    EXPECT_FALSE(info.permission(QFile::WriteOther));

    ASSERT_TRUE(writeQuirks(path, QString()));
    EXPECT_EQ(readQuirks(path), std::optional<QString>(QString()));
}

TEST(ut_IppUsbBlacklist, oversizedFileIsRefused)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("big.conf");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(2 * 1024 * 1024, 'x'));
    file.close();
    EXPECT_FALSE(readQuirks(path).has_value());
}

TEST(ut_IppUsbBlacklist, writeFailsInUnwritableLocation)
{
    EXPECT_FALSE(writeQuirks("/proc/dde-printer-test/quirks.conf", "x"));
}

TEST(ut_IppUsbBlacklist, statusFromStatusOutput)
{
    EXPECT_EQ(statusFromStatusOutput(kStatus, "232b:0ef5"), QString("OK"));
    EXPECT_EQ(statusFromStatusOutput(kStatus, "04b8:0e1a"), QString("Failed"));
    EXPECT_TRUE(statusFromStatusOutput(kStatus, "ffff:0000").isEmpty());
}

TEST(ut_IppUsbBlacklist, statusMissingOrMalformedIsEmpty)
{
    EXPECT_TRUE(statusFromStatusOutput("   1. Bus 001 Device 018  232b:0ef5  \"P\"", "232b:0ef5").isEmpty());
    EXPECT_TRUE(statusFromStatusOutput("   1. Bus 001 Device 018  232b:0ef5  \"P\"\n      note: x\n", "232b:0ef5").isEmpty());
    EXPECT_TRUE(statusFromStatusOutput("", "232b:0ef5").isEmpty());
}

TEST(ut_IppUsbBlacklist, statusParsingToleratesCaseAndCrlf)
{
    EXPECT_EQ(statusFromStatusOutput("   1. Bus 001 Device 018  232B:0EF5  \"P\"\r\n      Status: ok\r\n", "232b:0ef5"),
              QString("ok"));
}

// ---------------------------------------------------- add() / remove()

namespace {

const char kEpsonId[] = "04b8:0e1a";
const char kHealthyId[] = "232b:0ef5";

class ut_IppUsbBlacklistRun : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tools.isValid());
        ASSERT_TRUE(m_dir.isValid());
        m_quirks = m_dir.filePath("quirks/dde-printer-blacklist.conf");
        setQuirksFile(m_quirks);
    }
    void TearDown() override { setQuirksFile(QString()); }

    void installIppUsb(const QString &statusText, int exitCode = 0)
    {
        QFile out(m_tools.dir() + "/status.txt");
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(statusText.toUtf8());
        out.close();
        m_tools.add("ipp-usb", "cat '" + m_tools.dir() + "/status.txt'; exit " + QString::number(exitCode));
    }

    void installSystemctl(int exitCode = 0) { m_tools.add("systemctl", "exit " + QString::number(exitCode)); }

    int runAdd(const QString &id)
    {
        QObject owner;
        const auto waited = waitForResult<int>([&](std::function<void(int)> cb) { add(&owner, id, cb); });
        EXPECT_EQ(waited.calls, 1) << "done must be called exactly once";
        return waited.value.value_or(-99);
    }

    int runRemove(const QString &id)
    {
        QObject owner;
        const auto waited = waitForResult<int>([&](std::function<void(int)> cb) { remove(&owner, id, cb); });
        EXPECT_EQ(waited.calls, 1) << "done must be called exactly once";
        return waited.value.value_or(-99);
    }

    QString quirksText() const { return readQuirks(m_quirks).value_or(QString("<unreadable>")); }

    FakeTools m_tools;
    QTemporaryDir m_dir;
    QString m_quirks;
};

}  // namespace

TEST_F(ut_IppUsbBlacklistRun, brokenDeviceIsBlacklistedAndIppUsbRestarted)
{
    installIppUsb(kStatus);
    installSystemctl();
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kOk));
    EXPECT_EQ(quirksText(), withEntry(QString(), kEpsonId, "EPSON ET-2820 Series"));
    EXPECT_EQ(m_tools.calls(), (QList<QStringList>{{"ipp-usb", "status"}, {"systemctl", "restart", "ipp-usb"}}));

    const QFileInfo info(m_quirks);
    EXPECT_TRUE(info.permission(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther));
    EXPECT_FALSE(info.permission(QFile::WriteGroup | QFile::WriteOther));
}

TEST_F(ut_IppUsbBlacklistRun, secondDeviceIsAppendedToTheExistingFile)
{
    installIppUsb(kStatus);
    installSystemctl();
    ASSERT_TRUE(writeQuirks(m_quirks, withEntry(QString(), "1111:2222", "Other")));
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kOk));
    EXPECT_TRUE(hasEntry(quirksText(), "1111:2222"));
    EXPECT_TRUE(hasEntry(quirksText(), kEpsonId));
}

TEST_F(ut_IppUsbBlacklistRun, alreadyBlacklistedDeviceNeedsNoCommand)
{
    ASSERT_TRUE(writeQuirks(m_quirks, withEntry(QString(), kEpsonId, "EPSON ET-2820 Series")));
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kOk));
    EXPECT_TRUE(m_tools.calls().isEmpty());
}

TEST_F(ut_IppUsbBlacklistRun, workingDeviceIsNotBlacklisted)
{
    installIppUsb(kStatus);
    installSystemctl();
    EXPECT_EQ(runAdd(kHealthyId), static_cast<int>(kDeviceHealthy));
    EXPECT_FALSE(QFileInfo::exists(m_quirks));
    EXPECT_TRUE(m_tools.callsOf("systemctl").isEmpty());
}

TEST_F(ut_IppUsbBlacklistRun, unlistedDeviceIsReported)
{
    installIppUsb(kStatus);
    EXPECT_EQ(runAdd("ffff:0000"), static_cast<int>(kNotListed));
}

TEST_F(ut_IppUsbBlacklistRun, failingStatusCommandIsAFailure)
{
    installIppUsb(kStatus, 1);
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kFailed));
}

TEST_F(ut_IppUsbBlacklistRun, missingIppUsbIsReported)
{
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kNotInstalled));
}

TEST_F(ut_IppUsbBlacklistRun, modelThatCouldCorruptTheFileIsRefused)
{
    installIppUsb("   1. Bus 001 Device 019  04B8:0E1A  \"Evil]\\n[global\"\n      status: Failed\n");
    installSystemctl();
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kFailed));
    EXPECT_FALSE(QFileInfo::exists(m_quirks));
}

TEST_F(ut_IppUsbBlacklistRun, restartFailureIsReportedAfterTheEntryWasWritten)
{
    installIppUsb(kStatus);
    installSystemctl(1);
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kRestartFailed));
    EXPECT_TRUE(hasEntry(quirksText(), kEpsonId));
}

TEST_F(ut_IppUsbBlacklistRun, missingSystemctlCountsAsRestartFailure)
{
    installIppUsb(kStatus);
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kRestartFailed));
}

TEST_F(ut_IppUsbBlacklistRun, unwritableQuirksLocationIsReported)
{
    installIppUsb(kStatus);
    installSystemctl();
    // the parent "directory" is a regular file
    QFile blocker(m_dir.filePath("blocker"));
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    setQuirksFile(m_dir.filePath("blocker/quirks.conf"));
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kWriteFailed));
    EXPECT_TRUE(m_tools.callsOf("systemctl").isEmpty());
}

TEST_F(ut_IppUsbBlacklistRun, oversizedQuirksFileStopsAddAndRemove)
{
    ASSERT_TRUE(QDir().mkpath(QFileInfo(m_quirks).absolutePath()));
    QFile big(m_quirks);
    ASSERT_TRUE(big.open(QIODevice::WriteOnly));
    big.write(QByteArray(2 * 1024 * 1024, 'x'));
    big.close();
    installIppUsb(kStatus);
    EXPECT_EQ(runAdd(kEpsonId), static_cast<int>(kFailed));
    EXPECT_EQ(runRemove(kEpsonId), static_cast<int>(kFailed));
    EXPECT_TRUE(m_tools.calls().isEmpty());
}

TEST_F(ut_IppUsbBlacklistRun, removeDeletesOnlyTheRequestedEntry)
{
    installSystemctl();
    QString content = withEntry(QString(), "1111:2222", "Other");
    content = withEntry(content, kEpsonId, "EPSON ET-2820 Series");
    ASSERT_TRUE(writeQuirks(m_quirks, content));

    EXPECT_EQ(runRemove(kEpsonId), static_cast<int>(kOk));
    EXPECT_EQ(quirksText(), withEntry(QString(), "1111:2222", "Other"));
    EXPECT_EQ(m_tools.calls(), (QList<QStringList>{{"systemctl", "restart", "ipp-usb"}}));
}

TEST_F(ut_IppUsbBlacklistRun, removeOfUnknownDeviceTouchesNothing)
{
    EXPECT_EQ(runRemove(kEpsonId), static_cast<int>(kOk));
    EXPECT_FALSE(QFileInfo::exists(m_quirks));
    EXPECT_TRUE(m_tools.calls().isEmpty());
}

TEST_F(ut_IppUsbBlacklistRun, removeReportsRestartAndWriteFailures)
{
    ASSERT_TRUE(writeQuirks(m_quirks, withEntry(QString(), kEpsonId, "A")));
    installSystemctl(1);
    EXPECT_EQ(runRemove(kEpsonId), static_cast<int>(kRestartFailed));
    EXPECT_FALSE(hasEntry(quirksText(), kEpsonId));
}
