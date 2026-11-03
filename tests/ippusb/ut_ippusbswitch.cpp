// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "ippusbswitch.h"

// The environment seam replaces avahi, the privileged service and the CUPS
// device list, so the success paths run without hardware.

namespace {

struct EnvGuard
{
    ~EnvGuard() { IppUsbSwitch::resetEnvironment(); }
};

AvahiTool::IppUsbDevice device(const QString &vendor, const QString &product, const QString &model)
{
    AvahiTool::IppUsbDevice d;
    d.vendorId = vendor;
    d.productId = product;
    d.model = model;
    d.status = "OK";
    return d;
}

}   // namespace

TEST(ut_IppUsbSwitch, driverlessWithoutPublishedServiceFails)
{
    const IppUsbSwitch::Result result = IppUsbSwitch::driverlessUri("ipp://localhost:60999/ipp/print");
    if (result.ok)
        GTEST_SKIP() << "an ipp-usb service is really published on this machine";
    EXPECT_TRUE(result.uri.isEmpty());
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(ut_IppUsbSwitch, driverlessWithGarbageUriFails)
{
    const IppUsbSwitch::Result result = IppUsbSwitch::driverlessUri(QString());
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.uri.isEmpty());
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(ut_IppUsbSwitch, usbBackendForAnUnlistedModelFailsBeforeTouchingTheBlacklist)
{
    const IppUsbSwitch::Result result = IppUsbSwitch::usbBackendUri("Ut Zz Nonexistent Model 000");
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.uri.isEmpty());
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(ut_IppUsbSwitch, driverlessReturnsTheDnssdUriOfTheService)
{
    EnvGuard guard;
    QString asked;
    IppUsbSwitch::Environment env;
    env.dnssdUri = [&asked](const QString &uri) {
        asked = uri;
        return QString("dnssd://Foo%20Bar._ipp._tcp.local/?uuid=1");
    };
    IppUsbSwitch::setEnvironment(env);

    const IppUsbSwitch::Result result = IppUsbSwitch::driverlessUri("ipp://localhost:60000/ipp/print");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.uri, QString("dnssd://Foo%20Bar._ipp._tcp.local/?uuid=1"));
    EXPECT_TRUE(result.error.isEmpty());
    EXPECT_EQ(asked, QString("ipp://localhost:60000/ipp/print"));
}

TEST(ut_IppUsbSwitch, driverlessFailsWhenAvahiKnowsNothing)
{
    EnvGuard guard;
    IppUsbSwitch::Environment env;
    env.dnssdUri = [](const QString &) { return QString(); };
    IppUsbSwitch::setEnvironment(env);

    const IppUsbSwitch::Result result = IppUsbSwitch::driverlessUri("ipp://localhost:60000/ipp/print");
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.uri.isEmpty());
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(ut_IppUsbSwitch, usbBackendBlacklistsTheDeviceAndFindsTheUsbAddress)
{
    EnvGuard guard;
    QStringList blacklisted;
    IppUsbSwitch::Environment env;
    env.ippUsbDevices = []() {
        return QList<AvahiTool::IppUsbDevice>{device("04b8", "1234", "Other Printer"),
                                              device("03f0", "c211", "HP LaserJet M100")};
    };
    env.addToBlacklist = [&blacklisted](const QString &vp) {
        blacklisted << vp;
        return IppUsbClient::Outcome::Ok;
    };
    env.usbBackendUris = []() {
        return QStringList{"usb://Epson/Something?serial=1", "usb://HP/LaserJet%20M100?serial=ABC"};
    };
    env.attempts = 2;
    env.pauseMs = 1;
    IppUsbSwitch::setEnvironment(env);

    const IppUsbSwitch::Result result = IppUsbSwitch::usbBackendUri("HP LaserJet M100");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.uri, QString("usb://HP/LaserJet%20M100?serial=ABC"));
    EXPECT_EQ(blacklisted, QStringList{"03f0:c211"});
}

TEST(ut_IppUsbSwitch, usbBackendReportsABlacklistFailure)
{
    EnvGuard guard;
    IppUsbSwitch::Environment env;
    env.ippUsbDevices = []() { return QList<AvahiTool::IppUsbDevice>{device("03f0", "c211", "HP LaserJet M100")}; };
    env.addToBlacklist = [](const QString &) { return IppUsbClient::Outcome::Denied; };
    bool listed = false;
    env.usbBackendUris = [&listed]() {
        listed = true;
        return QStringList();
    };
    IppUsbSwitch::setEnvironment(env);

    const IppUsbSwitch::Result result = IppUsbSwitch::usbBackendUri("HP LaserJet M100");
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.uri.isEmpty());
    EXPECT_FALSE(result.error.isEmpty());
    EXPECT_FALSE(listed);   // no point waiting for an address after a refusal
}

TEST(ut_IppUsbSwitch, usbBackendGivesUpWhenTheAddressNeverAppears)
{
    EnvGuard guard;
    int polls = 0;
    IppUsbSwitch::Environment env;
    env.ippUsbDevices = []() { return QList<AvahiTool::IppUsbDevice>{device("03f0", "c211", "HP LaserJet M100")}; };
    env.addToBlacklist = [](const QString &) { return IppUsbClient::Outcome::Ok; };
    env.usbBackendUris = [&polls]() {
        ++polls;
        return QStringList{"usb://Epson/Something"};
    };
    env.attempts = 3;
    env.pauseMs = 1;
    IppUsbSwitch::setEnvironment(env);

    const IppUsbSwitch::Result result = IppUsbSwitch::usbBackendUri("HP LaserJet M100");
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.isEmpty());
    EXPECT_EQ(polls, 3);
}

TEST(ut_IppUsbSwitch, usbBackendFindsTheAddressOnALaterAttempt)
{
    EnvGuard guard;
    int polls = 0;
    IppUsbSwitch::Environment env;
    env.ippUsbDevices = []() { return QList<AvahiTool::IppUsbDevice>{device("03f0", "c211", "HP LaserJet M100")}; };
    env.addToBlacklist = [](const QString &) { return IppUsbClient::Outcome::Ok; };
    env.usbBackendUris = [&polls]() {
        return ++polls < 3 ? QStringList() : QStringList{"hp:/usb/HP_LaserJet_M100?serial=1"};
    };
    env.attempts = 5;
    env.pauseMs = 1;
    IppUsbSwitch::setEnvironment(env);

    const IppUsbSwitch::Result result = IppUsbSwitch::usbBackendUri("HP LaserJet M100");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.uri, QString("hp:/usb/HP_LaserJet_M100?serial=1"));
    EXPECT_EQ(polls, 3);
}
