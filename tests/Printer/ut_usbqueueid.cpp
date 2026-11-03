// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usbqueueid.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace UsbQueueId;

namespace {

SysfsDevice device(const QString &vp, const QString &maker, const QString &product, const QString &serial)
{
    SysfsDevice d;
    d.vendorProduct = vp;
    d.manufacturer = maker;
    d.product = product;
    d.serial = serial;
    return d;
}

void writeNode(const QDir &dir, const QString &name, const QString &value)
{
    QFile file(dir.filePath(name));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(value.toUtf8());
}

}  // namespace

TEST(ut_UsbQueueId, serialFromUriReadsTheQuery)
{
    EXPECT_EQ(serialFromUri("usb://Pantum/P3301DN%20series?serial=ABC123"), QString("ABC123"));
    EXPECT_EQ(serialFromUri("hp:/usb/HP_LaserJet?serial=CN%2001"), QString("CN 01"));
    EXPECT_TRUE(serialFromUri("usb://Pantum/P3301DN").isEmpty());
    EXPECT_TRUE(serialFromUri("").isEmpty());
}

TEST(ut_UsbQueueId, serialDecidesBetweenDevicesOfTheSameModel)
{
    const QList<SysfsDevice> devices = {
        device("232b:0ef5", "Pantum", "P3301DN series", "S1"),
        device("232b:0ef6", "Pantum", "P3301DN series", "S2"),
    };
    EXPECT_EQ(vendorProductForUri("usb://Pantum/P3301DN%20series?serial=S2", devices), QString("232b:0ef6"));
}

TEST(ut_UsbQueueId, modelIsUsedWhenThereIsNoSerialMatch)
{
    const QList<SysfsDevice> devices = {
        device("04b8:0e1a", "EPSON", "L3150 Series", ""),
        device("232b:0ef5", "Pantum", "P3301DN series", ""),
    };
    EXPECT_EQ(vendorProductForUri("usb://EPSON/L3150%20Series", devices), QString("04b8:0e1a"));
    EXPECT_EQ(vendorProductForUri("usb://Pantum/P3301DN%20series?serial=GONE", devices), QString("232b:0ef5"));
}

TEST(ut_UsbQueueId, ambiguousOrUnknownGivesNothing)
{
    const QList<SysfsDevice> twins = {
        device("232b:0ef5", "Pantum", "P3301DN series", ""),
        device("232b:0ef6", "Pantum", "P3301DN series", ""),
    };
    EXPECT_TRUE(vendorProductForUri("usb://Pantum/P3301DN%20series", twins).isEmpty());
    EXPECT_TRUE(vendorProductForUri("usb://Unknown/Thing", twins).isEmpty());
    EXPECT_TRUE(vendorProductForUri("usb://Pantum/P3301DN%20series", {}).isEmpty());
}

TEST(ut_UsbQueueId, nonUsbUrisAreIgnored)
{
    const QList<SysfsDevice> devices = {device("232b:0ef5", "Pantum", "P3301DN series", "S1")};
    EXPECT_TRUE(vendorProductForUri("ipp://localhost:60000/ipp/print", devices).isEmpty());
    EXPECT_TRUE(vendorProductForUri("dnssd://Pantum._ipp._tcp.local/", devices).isEmpty());
    EXPECT_TRUE(vendorProductForUri("", devices).isEmpty());
}

TEST(ut_UsbQueueId, quirksMarkerLookup)
{
    const QString content = "; dde-printer ippusb 232b:0ef5\n[Pantum P3301DN series]\nblacklist = true\n";
    EXPECT_TRUE(quirksListDevice(content, "232b:0ef5"));
    EXPECT_TRUE(quirksListDevice(content, "232B:0EF5"));
    EXPECT_FALSE(quirksListDevice(content, "232b:0ef6"));
    EXPECT_FALSE(quirksListDevice(QString(), "232b:0ef5"));
}

TEST(ut_UsbQueueId, readSysfsDevicesSkipsInterfacesAndBadIds)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QDir base(root.path());
    ASSERT_TRUE(base.mkpath("1-2"));
    ASSERT_TRUE(base.mkpath("1-2:1.0"));
    ASSERT_TRUE(base.mkpath("1-3"));
    const QDir good(base.filePath("1-2"));
    writeNode(good, "idVendor", "232b\n");
    writeNode(good, "idProduct", "0EF5\n");
    writeNode(good, "manufacturer", "Pantum\n");
    writeNode(good, "product", "P3301DN series\n");
    writeNode(good, "serial", "S1\n");
    const QDir bad(base.filePath("1-3"));
    writeNode(bad, "idVendor", "zz");
    writeNode(bad, "idProduct", "0001");

    const QList<SysfsDevice> devices = readSysfsDevices(root.path());
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices.first().vendorProduct, QString("232b:0ef5"));
    EXPECT_EQ(devices.first().product, QString("P3301DN series"));
    EXPECT_EQ(devices.first().serial, QString("S1"));
    EXPECT_TRUE(readSysfsDevices(root.path() + "/missing").isEmpty());
}

TEST(ut_UsbQueueId, releaseIgnoresUrisThatAreNotUsbBackend)
{
    EXPECT_FALSE(releaseFromBlacklist("ipp://localhost:60000/ipp/print"));
    EXPECT_FALSE(releaseFromBlacklist(""));
}
