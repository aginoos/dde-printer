// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>

#include "avahi_tool.h"

using namespace AvahiTool;

TEST(ut_AvahiTool, buildDnssdUriEncodesName)
{
    EXPECT_EQ(buildDnssdUri("Canon iR2006", "825e49a2"),
              QString("dnssd://Canon%20iR2006._ipp._tcp.local/?uuid=825e49a2"));
}

TEST(ut_AvahiTool, buildDnssdUriKeepsSlashInNameFromBeingTakenAsPlaceholder)
{
    // "%2F" in the encoded name must survive and must not swallow the uuid
    const QString uri = buildDnssdUri("Model A/B", "abc");
    EXPECT_TRUE(uri.contains("Model%20A%2FB._ipp._tcp.local/"));
    EXPECT_TRUE(uri.endsWith("?uuid=abc"));
}

TEST(ut_AvahiTool, buildDnssdUriWithoutUuid)
{
    EXPECT_EQ(buildDnssdUri("P", QString(), "_ipps._tcp"), QString("dnssd://P._ipps._tcp.local/"));
}

TEST(ut_AvahiTool, parseDnssdNameRoundTrip)
{
    const DnssdName name = parseDnssdName(buildDnssdUri("Model A/B @ host.x", "abc"));
    ASSERT_TRUE(name.isValid());
    EXPECT_EQ(name.instance, QString("Model A/B @ host.x"));
    EXPECT_EQ(name.type, QString("_ipp._tcp"));
    EXPECT_EQ(name.domain, QString("local"));
}

TEST(ut_AvahiTool, parseDnssdNameRejectsOtherSchemes)
{
    EXPECT_FALSE(parseDnssdName("ipp://localhost:631/ipp/print").isValid());
    EXPECT_FALSE(parseDnssdName("dnssd://nothing-here/").isValid());
}

TEST(ut_AvahiTool, isIppUsbUri)
{
    EXPECT_TRUE(isIppUsbUri("ipp://localhost:60009/ipp/print"));
    EXPECT_TRUE(isIppUsbUri("ipp://127.0.0.1:60000/ipp/print"));
    EXPECT_FALSE(isIppUsbUri("ipp://localhost:631/printers/a"));
    EXPECT_FALSE(isIppUsbUri("ipp://192.168.1.2:60000/ipp/print"));
    EXPECT_FALSE(isIppUsbUri("socket://127.0.0.1:60000"));
}

TEST(ut_AvahiTool, serviceRecordModelPrefersTxt)
{
    ServiceRecord record;
    record.name = "Pantum P3301DN Series 6076A2 (USB)";
    EXPECT_EQ(record.model(), QString("Pantum P3301DN Series"));
    record.txt.insert("ty", "Pantum P3301DN series");
    EXPECT_EQ(record.model(), QString("Pantum P3301DN series"));
}

TEST(ut_AvahiTool, serviceRecordKeepsShortHexLikeModelNumbers)
{
    ServiceRecord record;
    record.name = "Xerox B210";
    EXPECT_EQ(record.model(), QString("Xerox B210"));
}

TEST(ut_AvahiTool, serviceRecordIppUsbDetection)
{
    ServiceRecord record;
    record.address = "127.0.0.1";
    record.txt.insert("usb_mfg", "Pantum");
    EXPECT_TRUE(record.isIppUsb());
    record.txt.insert("printer-type", "0x1234");
    EXPECT_FALSE(record.isIppUsb());
}
