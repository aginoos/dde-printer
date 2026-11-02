// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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
