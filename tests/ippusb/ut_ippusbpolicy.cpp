// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "ippusbpolicy.h"

using namespace IppUsbPolicy;
using AvahiTool::IppUsbDevice;
using AvahiTool::ServiceRecord;

namespace {

IppUsbDevice device(const char *vid, const char *pid, const char *model, const char *status)
{
    IppUsbDevice d;
    d.vendorId = vid;
    d.productId = pid;
    d.model = model;
    d.status = status;
    return d;
}

ServiceRecord service(const QString &name, quint16 port, const QString &uuid = QStringLiteral("u1"))
{
    ServiceRecord r;
    r.name = name;
    r.type = QStringLiteral("_ipp._tcp");
    r.port = port;
    r.txt.insert(QStringLiteral("uuid"), uuid);
    r.txt.insert(QStringLiteral("ty"), name);
    return r;
}

}

TEST(ut_IppUsbPolicy, vendorProductIdIsLowerCaseAndPadded)
{
    EXPECT_EQ(vendorProductId(0x04b8, 0x0e1a), QString("04b8:0e1a"));
    EXPECT_EQ(vendorProductId(0x0001, 0x00ff), QString("0001:00ff"));
}

TEST(ut_IppUsbPolicy, sanitizeRemovesControlCharactersAndCollapsesSpace)
{
    EXPECT_EQ(sanitizeDeviceString(QString("  HP\tLaser\n  Jet\x01 ")), QString("HP Laser Jet"));
    EXPECT_EQ(sanitizeDeviceString(QString("\x01\x02")), QString());
}

TEST(ut_IppUsbPolicy, sanitizeCutsLongStrings)
{
    const QString cut = sanitizeDeviceString(QString(300, 'a'), 64);
    EXPECT_EQ(cut.size(), 64);
}

TEST(ut_IppUsbPolicy, composeModelDoesNotRepeatManufacturer)
{
    EXPECT_EQ(composeModel("Pantum", "P3301DN series"), QString("Pantum P3301DN series"));
    EXPECT_EQ(composeModel("HP", "HP LaserJet M404"), QString("HP LaserJet M404"));
    EXPECT_EQ(composeModel("", "X1"), QString("X1"));
    EXPECT_EQ(composeModel("Canon", ""), QString("Canon"));
}

TEST(ut_IppUsbPolicy, buildDeviceIdStripsSeparators)
{
    EXPECT_EQ(buildDeviceId("Evil;MDL:x", "Model:1;"), QString("MFG:EvilMDLx;MDL:Model1;"));
}

TEST(ut_IppUsbPolicy, modelsMatchIsLenient)
{
    EXPECT_TRUE(modelsMatch("Pantum P3301DN series", "pantum  p3301dn SERIES"));
    EXPECT_TRUE(modelsMatch("HP LaserJet Pro M404", "LaserJet Pro M404"));
    EXPECT_FALSE(modelsMatch("Pantum P3301DN", "Canon LBP"));
    EXPECT_FALSE(modelsMatch("", "Canon"));
}

TEST(ut_IppUsbPolicy, lookupDeviceStates)
{
    const QList<IppUsbDevice> list{device("232b", "0ef5", "Pantum P3301DN series", "OK"),
                                   device("04b8", "0e1a", "EPSON X", "Error: busy")};
    EXPECT_EQ(lookupDevice(list, "232b:0ef5").health, Health::Healthy);
    EXPECT_EQ(lookupDevice(list, "232b:0ef5").model, QString("Pantum P3301DN series"));
    EXPECT_EQ(lookupDevice(list, "04B8:0E1A").health, Health::Unhealthy);
    EXPECT_EQ(lookupDevice(list, "ffff:0001").health, Health::NotListed);
    EXPECT_EQ(lookupDevice({}, "232b:0ef5").health, Health::NotListed);
}

TEST(ut_IppUsbPolicy, lookupDeviceWithTwinsIsHealthyOnlyIfAllAre)
{
    const QList<IppUsbDevice> list{device("232b", "0ef5", "P", "OK"), device("232b", "0ef5", "P", "Failed")};
    EXPECT_EQ(lookupDevice(list, "232b:0ef5").health, Health::Unhealthy);
    const QList<IppUsbDevice> reversed{device("232b", "0ef5", "P", "Failed"), device("232b", "0ef5", "P", "OK")};
    EXPECT_EQ(lookupDevice(reversed, "232b:0ef5").health, Health::Unhealthy);
}

TEST(ut_IppUsbPolicy, isDnssdServiceUri)
{
    EXPECT_TRUE(isDnssdServiceUri("dnssd://Pantum%20P3301._ipp._tcp.local/?uuid=1"));
    EXPECT_TRUE(isDnssdServiceUri("dnssd://X._ipps._tcp.local/"));
    EXPECT_FALSE(isDnssdServiceUri("dnssd://X._printer._tcp.local/"));
    EXPECT_TRUE(isDnssdServiceUri("ipp://Pantum%20P3301._ipp._tcp/"));
    EXPECT_TRUE(isDnssdServiceUri("ipp://Pantum P3301._ipp._tcp.local/ipp/print"));
    EXPECT_FALSE(isDnssdServiceUri("ipp://localhost:60000/ipp/print"));
    EXPECT_FALSE(isDnssdServiceUri("usb://Pantum/P3301?serial=1"));
}

TEST(ut_IppUsbPolicy, sameDnssdServiceIgnoresUuidAndCase)
{
    EXPECT_TRUE(sameDnssdService("dnssd://A%20B._ipp._tcp.local/?uuid=1", "dnssd://a%20b._ipp._tcp.local/"));
    EXPECT_FALSE(sameDnssdService("dnssd://A._ipp._tcp.local/", "dnssd://B._ipp._tcp.local/"));
    EXPECT_FALSE(sameDnssdService("ipp://x/", "dnssd://B._ipp._tcp.local/"));
}

TEST(ut_IppUsbPolicy, queueCoversServiceByDnssdPortAndHost)
{
    const ServiceRecord svc = service("Pantum P3301DN series", 60001);
    EXPECT_TRUE(queueCoversService(dnssdUriForService(svc), svc));
    EXPECT_TRUE(queueCoversService("ipp://Pantum%20P3301DN%20series._ipp._tcp.local/", svc));
    EXPECT_TRUE(queueCoversService("ipp://localhost:60001/ipp/print", svc));
    EXPECT_FALSE(queueCoversService("ipp://localhost:60002/ipp/print", svc));
    EXPECT_FALSE(queueCoversService("ipp://192.168.1.5:60001/ipp/print", svc));
    EXPECT_FALSE(queueCoversService("usb://Pantum/P3301DN%20series?serial=1", svc));
}

TEST(ut_IppUsbPolicy, unusedServicesSkipsCoveredOnes)
{
    const ServiceRecord a = service("Pantum P3301DN series", 60000, "a");
    const ServiceRecord b = service("Pantum P3301DN series (2)", 60001, "b");
    const ServiceRecord other = service("Canon LBP", 60002, "c");
    const QList<ServiceRecord> all{a, b, other};

    EXPECT_EQ(unusedServices(all, "Pantum P3301DN series", {}).size(), 2);
    const QList<ServiceRecord> left = unusedServices(all, "Pantum P3301DN series", {dnssdUriForService(a)});
    ASSERT_EQ(left.size(), 1);
    EXPECT_EQ(left.first().name, b.name);
    EXPECT_TRUE(unusedServices(all, "Unknown", {}).isEmpty());
}

TEST(ut_IppUsbPolicy, dnssdUriForServiceCarriesUuid)
{
    const QString uri = dnssdUriForService(service("A B", 1, "xyz"));
    EXPECT_EQ(uri, QString("dnssd://A%20B._ipp._tcp.local/?uuid=xyz"));
}

TEST(ut_IppUsbPolicy, modelFromUsbUri)
{
    EXPECT_EQ(modelFromUsbUri("usb://Pantum/P3301DN%20series?serial=CF6L"), QString("Pantum P3301DN series"));
    EXPECT_EQ(modelFromUsbUri("hp:/usb/HP_LaserJet_M404?serial=1"), QString("HP LaserJet M404"));
    EXPECT_EQ(modelFromUsbUri("ipp://localhost:60000/"), QString());
}

TEST(ut_IppUsbPolicy, isUsbBackendUriFor)
{
    EXPECT_TRUE(isUsbBackendUriFor("usb://Pantum/P?serial=CF6L088530", "cf6l088530"));
    EXPECT_TRUE(isUsbBackendUriFor("hp:/usb/HP?serial=ABC", "ABC"));
    EXPECT_TRUE(isUsbBackendUriFor("ccp://localhost:59687/usb/ABC", "abc"));
    EXPECT_FALSE(isUsbBackendUriFor("ccp://localhost:59687/usb/ABC", "XYZ"));
    EXPECT_FALSE(isUsbBackendUriFor("ipp://localhost/?serial=ABC", "ABC"));
    EXPECT_FALSE(isUsbBackendUriFor("usb://Pantum/P?serial=1", ""));
    EXPECT_FALSE(isUsbBackendUriFor("usb://Pantum/P?serial=1", "2"));
}
