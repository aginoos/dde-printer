// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printeruihelpers.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include "zdrivermanager.h"

using namespace PrinterUi;

TEST(ut_PrinterUiHelpers, hplipPackageIsTheDebianName)
{
    EXPECT_STREQ(kHplipPackage, "hplip");
}

TEST(ut_PrinterUiHelpers, firstExecutablePicksTheFirstUsableFile)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString plain = dir.filePath("plain");
    const QString tool = dir.filePath("tool");
    for (const QString &path : {plain, tool}) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n");
    }
    ASSERT_TRUE(QFile::setPermissions(plain, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    ASSERT_TRUE(QFile::setPermissions(tool, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    EXPECT_EQ(firstExecutable({dir.filePath("missing"), plain, tool, dir.path()}), tool);
    EXPECT_TRUE(firstExecutable({dir.filePath("missing"), plain, dir.path()}).isEmpty());
    EXPECT_TRUE(firstExecutable({}).isEmpty());
}

TEST(ut_PrinterUiHelpers, driverOriginFollowsTheSource)
{
    EXPECT_EQ(driverOrigin(PPDFrom_Database, false), DriverOrigin::Local);
    EXPECT_EQ(driverOrigin(PPDFrom_Server, true), DriverOrigin::Online);
    EXPECT_EQ(driverOrigin(PPDFrom_Server, false), DriverOrigin::Local);
    EXPECT_EQ(driverOrigin(PPDFrom_EveryWhere, false), DriverOrigin::Driverless);
    EXPECT_EQ(driverOrigin(PPDFrom_File, false), DriverOrigin::None);
    EXPECT_EQ(driverOrigin(99, true), DriverOrigin::None);
}

TEST(ut_PrinterUiHelpers, driverDisplayNameAddsTheLabelOnce)
{
    EXPECT_EQ(driverDisplayName("HP LaserJet", DriverOrigin::Local), QString("HP LaserJet (Local Match)"));
    EXPECT_EQ(driverDisplayName("HP LaserJet", DriverOrigin::Online), QString("HP LaserJet (Online Match)"));
    EXPECT_EQ(driverDisplayName("HP LaserJet (Local Match)", DriverOrigin::Local), QString("HP LaserJet (Local Match)"));
    EXPECT_EQ(driverDisplayName("My file", DriverOrigin::None), QString("My file"));
}

TEST(ut_PrinterUiHelpers, driverlessEntryHasItsOwnName)
{
    EXPECT_EQ(driverDisplayName("EveryWhere driver", DriverOrigin::Driverless), QString("Use Driverless Solution"));
}

TEST(ut_PrinterUiHelpers, vendorListIsCompleteAndWellFormed)
{
    const QList<VendorEntry> &vendors = driverVendors();
    EXPECT_EQ(vendors.size(), 19);

    QSet<QString> names;
    for (const VendorEntry &vendor : vendors) {
        const QString name = QString::fromLatin1(vendor.name);
        const QString site = QString::fromLatin1(vendor.website);
        const QString logo = QString::fromLatin1(vendor.logo);
        EXPECT_FALSE(name.isEmpty());
        EXPECT_TRUE(site.startsWith("http://") || site.startsWith("https://")) << qPrintable(name);
        EXPECT_TRUE(logo.isEmpty() || (logo.startsWith(":/images/logo_") && logo.endsWith(".svg"))) << qPrintable(name);
        EXPECT_FALSE(names.contains(name)) << qPrintable(name);
        names.insert(name);
    }
    for (const char *wanted : {"HP", "Epson", "Ricoh", "Konica Minolta", "OKI", "Pantum", "Kyocera",
                               "Lexmark", "Sharp", "Riso", "Lenovo Image", "Godex"}) {
        EXPECT_TRUE(names.contains(QString::fromLatin1(wanted))) << wanted;
    }
}

TEST(ut_PrinterUiHelpers, badgeIsStableAndReadable)
{
    EXPECT_EQ(badgeLetter("pantum"), QString("P"));
    EXPECT_EQ(badgeLetter("  Godex"), QString("G"));
    EXPECT_TRUE(badgeLetter("").isEmpty());
    EXPECT_EQ(badgeRgb("Epson"), badgeRgb("Epson"));
    EXPECT_LE(badgeRgb("Epson"), 0xffffffu);
}
