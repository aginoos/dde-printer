// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "ippusbplanner.h"
#include "ippusbpolicy.h"

using namespace IppUsbPlanner;
using AvahiTool::IppUsbDevice;
using AvahiTool::ServiceRecord;

namespace {

const char kModel[] = "Pantum P3301DN series";

class FakeEnv : public PlannerEnv
{
public:
    std::optional<QStringList> queues = QStringList();
    QList<BackendDevice> backend;
    bool installed = true;
    QList<IppUsbDevice> listed;
    QList<ServiceRecord> services;
    IppUsbClient::Outcome blacklistOutcome = IppUsbClient::Outcome::Ok;
    // a successful blacklist makes the usb backend show the device
    QList<BackendDevice> backendAfterBlacklist;

    QStringList blacklisted;
    int pauses = 0;

    std::optional<QStringList> queueUris() override { return queues; }
    QList<BackendDevice> backendDevices() override { return backend; }
    bool ippUsbInstalled() override { return installed; }
    QList<IppUsbDevice> ippUsbDevices() override { return listed; }
    QList<ServiceRecord> ippUsbServices() override { return services; }
    IppUsbClient::Outcome blacklist(const QString &vp) override
    {
        blacklisted << vp;
        if (IppUsbClient::isSuccess(blacklistOutcome) && !backendAfterBlacklist.isEmpty())
            backend = backendAfterBlacklist;
        return blacklistOutcome;
    }
    void pause(int) override { ++pauses; }
};

UsbPrinter printer(bool ippUsb)
{
    UsbPrinter p;
    p.vendorId = 0x232b;
    p.productId = 0x0ef5;
    p.manufacturer = "Pantum";
    p.product = "P3301DN series";
    p.serial = "CF6L088530";
    p.ippUsbCapable = ippUsb;
    return p;
}

BackendDevice usbDevice()
{
    BackendDevice d;
    d.uri = "usb://Pantum/P3301DN%20series?serial=CF6L088530";
    d.deviceClass = "direct";
    d.info = kModel;
    d.makeAndModel = kModel;
    d.deviceId = "MFG:Pantum;MDL:P3301DN series;";
    d.location = "";
    return d;
}

IppUsbDevice listedDevice(const char *status)
{
    IppUsbDevice d;
    d.vendorId = "232b";
    d.productId = "0ef5";
    d.model = kModel;
    d.status = status;
    return d;
}

ServiceRecord publishedService()
{
    ServiceRecord r;
    r.name = kModel;
    r.type = "_ipp._tcp";
    r.port = 60000;
    r.txt.insert("uuid", "uuid-1");
    r.txt.insert("ty", kModel);
    return r;
}

Options fast()
{
    Options o;
    o.listingAttempts = 3;
    o.serviceAttempts = 3;
    o.backendAttempts = 3;
    return o;
}

}

TEST(ut_IppUsbPlanner, usbPrinterIdentity)
{
    EXPECT_EQ(printer(false).vendorProduct(), QString("232b:0ef5"));
    EXPECT_EQ(printer(false).model(), QString(kModel));
}

TEST(ut_IppUsbPlanner, incompleteIdentityIsSkipped)
{
    FakeEnv env;
    UsbPrinter p = printer(false);
    p.serial.clear();
    EXPECT_EQ(plan(p, env, fast()).kind, Plan::Skip);
    p = printer(false);
    p.manufacturer.clear();
    EXPECT_EQ(plan(p, env, fast()).kind, Plan::Skip);
}

TEST(ut_IppUsbPlanner, unreachableCupsIsSkipped)
{
    FakeEnv env;
    env.queues = std::nullopt;
    env.backend << usbDevice();
    EXPECT_EQ(plan(printer(false), env, fast()).kind, Plan::Skip);
}

TEST(ut_IppUsbPlanner, classicPrinterIsAddedThroughUsbBackend)
{
    FakeEnv env;
    env.backend << usbDevice();
    const Plan result = plan(printer(false), env, fast());
    ASSERT_EQ(result.kind, Plan::Traditional);
    EXPECT_EQ(result.device.uriList, QStringList(usbDevice().uri));
    EXPECT_EQ(result.device.strMakeAndModel, QString(kModel));
    EXPECT_EQ(result.device.serial, QString("CF6L088530"));
    EXPECT_EQ(result.device.iType, InfoFrom_Detect);
    EXPECT_TRUE(env.blacklisted.isEmpty());
}

TEST(ut_IppUsbPlanner, classicPrinterAlreadyQueuedIsSkipped)
{
    FakeEnv env;
    env.backend << usbDevice();
    env.queues = QStringList{usbDevice().uri};
    EXPECT_EQ(plan(printer(false), env, fast()).kind, Plan::Skip);
}

TEST(ut_IppUsbPlanner, classicPrinterMissingFromBackendIsSkipped)
{
    FakeEnv env;
    EXPECT_EQ(plan(printer(false), env, fast()).kind, Plan::Skip);
}

TEST(ut_IppUsbPlanner, hpAndUsbUrisAreMerged)
{
    FakeEnv env;
    BackendDevice hp = usbDevice();
    hp.uri = "hp:/usb/Pantum_P3301DN_series?serial=CF6L088530";
    env.backend << usbDevice() << hp;
    const Plan result = plan(printer(false), env, fast());
    ASSERT_EQ(result.kind, Plan::Traditional);
    EXPECT_EQ(result.device.uriList.size(), 2);
}

TEST(ut_IppUsbPlanner, healthyPublishedIppUsbPrinterIsAddedByDnssd)
{
    FakeEnv env;
    env.listed << listedDevice("OK");
    env.services << publishedService();
    const Plan result = plan(printer(true), env, fast());
    ASSERT_EQ(result.kind, Plan::IppUsb);
    EXPECT_EQ(result.device.uriList, QStringList("dnssd://Pantum%20P3301DN%20series._ipp._tcp.local/?uuid=uuid-1"));
    EXPECT_EQ(result.device.strDeviceId, QString("MFG:Pantum;MDL:P3301DN series;"));
    EXPECT_TRUE(env.blacklisted.isEmpty());
}

TEST(ut_IppUsbPlanner, ippUsbPrinterAlreadyAddedIsSkippedWithoutBlacklist)
{
    FakeEnv env;
    env.listed << listedDevice("OK");
    env.services << publishedService();
    env.queues = QStringList{"dnssd://Pantum%20P3301DN%20series._ipp._tcp.local/?uuid=uuid-1"};
    EXPECT_EQ(plan(printer(true), env, fast()).kind, Plan::Skip);
    EXPECT_TRUE(env.blacklisted.isEmpty());
}

TEST(ut_IppUsbPlanner, secondTwinGetsTheUnusedService)
{
    FakeEnv env;
    env.listed << listedDevice("OK");
    ServiceRecord second = publishedService();
    second.name = QString(kModel) + " (2)";
    second.port = 60001;
    second.txt["uuid"] = "uuid-2";
    second.txt["ty"] = kModel;
    env.services << publishedService() << second;
    env.queues = QStringList{"dnssd://Pantum%20P3301DN%20series._ipp._tcp.local/?uuid=uuid-1"};
    const Plan result = plan(printer(true), env, fast());
    ASSERT_EQ(result.kind, Plan::IppUsb);
    EXPECT_TRUE(result.device.uriList.first().contains("uuid-2"));
}

TEST(ut_IppUsbPlanner, existingUsbQueueBlacklistsIppUsbAndSkips)
{
    FakeEnv env;
    env.queues = QStringList{usbDevice().uri};
    const Plan result = plan(printer(true), env, fast());
    EXPECT_EQ(result.kind, Plan::Skip);
    EXPECT_EQ(env.blacklisted, QStringList("232b:0ef5"));
}

TEST(ut_IppUsbPlanner, unhealthyIppUsbIsBlacklistedAndFallsBack)
{
    FakeEnv env;
    env.listed << listedDevice("Failed");
    env.backendAfterBlacklist << usbDevice();
    const Plan result = plan(printer(true), env, fast());
    ASSERT_EQ(result.kind, Plan::Traditional);
    EXPECT_EQ(env.blacklisted, QStringList("232b:0ef5"));
}

TEST(ut_IppUsbPlanner, unpublishedIppUsbIsBlacklistedAndFallsBack)
{
    FakeEnv env;
    env.listed << listedDevice("OK");
    env.backendAfterBlacklist << usbDevice();
    const Plan result = plan(printer(true), env, fast());
    ASSERT_EQ(result.kind, Plan::Traditional);
    EXPECT_EQ(env.blacklisted, QStringList("232b:0ef5"));
    EXPECT_GE(env.pauses, 2);   // it waited for the publication
}

TEST(ut_IppUsbPlanner, refusedBlacklistFailsClosedWithoutWaiting)
{
    FakeEnv env;
    env.listed << listedDevice("Failed");
    env.blacklistOutcome = IppUsbClient::Outcome::Denied;
    const Plan result = plan(printer(true), env, fast());
    EXPECT_EQ(result.kind, Plan::Skip);
    EXPECT_EQ(env.blacklisted.size(), 1);
    EXPECT_TRUE(result.reason.contains("ipp-usb reports a failure"));
}

TEST(ut_IppUsbPlanner, refusedBlacklistStillUsesBackendIfItShowsTheDevice)
{
    FakeEnv env;
    env.listed << listedDevice("Failed");
    env.blacklistOutcome = IppUsbClient::Outcome::Denied;
    env.backend << usbDevice();
    EXPECT_EQ(plan(printer(true), env, fast()).kind, Plan::Traditional);
}

TEST(ut_IppUsbPlanner, deviceNotListedByIppUsbUsesBackendWithoutBlacklist)
{
    FakeEnv env;
    env.backend << usbDevice();
    const Plan result = plan(printer(true), env, fast());
    EXPECT_EQ(result.kind, Plan::Traditional);
    EXPECT_TRUE(env.blacklisted.isEmpty());
}

TEST(ut_IppUsbPlanner, capableButIppUsbNotInstalledIsClassic)
{
    FakeEnv env;
    env.installed = false;
    env.backend << usbDevice();
    EXPECT_EQ(plan(printer(true), env, fast()).kind, Plan::Traditional);
    EXPECT_TRUE(env.blacklisted.isEmpty());
}
