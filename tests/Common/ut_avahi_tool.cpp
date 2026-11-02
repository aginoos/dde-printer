// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>

// The implementation file is included (and left out of the build list, see
// Common.pri) so that the helpers in its unnamed namespace can be tested too.
#include "avahi_tool.cpp"

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

TEST(ut_AvahiTool, buildDnssdUriEncodesUnicodeAndReservedCharacters)
{
    EXPECT_EQ(buildDnssdUri(QString::fromUtf8("\xe6\x89\x93\xe5\x8d\xb0\xe6\x9c\xba"), "u"),
              QString("dnssd://%E6%89%93%E5%8D%B0%E6%9C%BA._ipp._tcp.local/?uuid=u"));
    EXPECT_EQ(buildDnssdUri("A @ b?c#d", QString()),
              QString("dnssd://A%20%40%20b%3Fc%23d._ipp._tcp.local/"));
}

TEST(ut_AvahiTool, buildDnssdUriHonoursServiceType)
{
    EXPECT_EQ(buildDnssdUri("P", "u", "_ipps._tcp"), QString("dnssd://P._ipps._tcp.local/?uuid=u"));
}

TEST(ut_AvahiTool, parseDnssdNameRoundTripUnicode)
{
    const QString instance = QString::fromUtf8("\xe6\x89\x93\xe5\x8d\xb0\xe6\x9c\xba 100%");
    const DnssdName name = parseDnssdName(buildDnssdUri(instance, "x"));
    ASSERT_TRUE(name.isValid());
    EXPECT_EQ(name.instance, instance);
}

TEST(ut_AvahiTool, parseDnssdNameServiceTypes)
{
    EXPECT_EQ(parseDnssdName("dnssd://P._ipps._tcp.local/").type, QString("_ipps._tcp"));
    EXPECT_EQ(parseDnssdName("dnssd://P._ipp-tls._tcp.local/").type, QString("_ipp-tls._tcp"));
    EXPECT_EQ(parseDnssdName("dnssd://P._printer._tcp.local/").type, QString("_printer._tcp"));
    EXPECT_EQ(parseDnssdName("dnssd://P._pdl-datastream._tcp.local/").type, QString("_pdl-datastream._tcp"));
}

TEST(ut_AvahiTool, parseDnssdNameStripsTrailingDotAndQuery)
{
    const DnssdName dotted = parseDnssdName("dnssd://X._ipp._tcp.local./");
    EXPECT_EQ(dotted.domain, QString("local"));

    const DnssdName query = parseDnssdName("dnssd://X._ipp._tcp.local?uuid=1");
    ASSERT_TRUE(query.isValid());
    EXPECT_EQ(query.instance, QString("X"));
    EXPECT_EQ(query.domain, QString("local"));
}

TEST(ut_AvahiTool, parseDnssdNameDomainWithSubLabels)
{
    const DnssdName name = parseDnssdName("dnssd://X._ipp._tcp.office.example.com/");
    EXPECT_EQ(name.domain, QString("office.example.com"));
}

TEST(ut_AvahiTool, parseDnssdNameEarliestMarkerWins)
{
    const DnssdName name = parseDnssdName("dnssd://A._ipp._tcp.b._printer._tcp.local/");
    EXPECT_EQ(name.instance, QString("A"));
    EXPECT_EQ(name.type, QString("_ipp._tcp"));
    EXPECT_EQ(name.domain, QString("b._printer._tcp.local"));
}

TEST(ut_AvahiTool, parseDnssdNameWithoutDomain)
{
    const DnssdName name = parseDnssdName("dnssd://Name._ipp._tcp");
    ASSERT_TRUE(name.isValid());
    EXPECT_EQ(name.instance, QString("Name"));
    EXPECT_EQ(name.type, QString("_ipp._tcp"));
    EXPECT_TRUE(name.domain.isEmpty());
}

TEST(ut_AvahiTool, parseDnssdNameKeepsCupsStyleInstance)
{
    const DnssdName name = parseDnssdName("dnssd://CUPS-PDF%20%40%20jinni-PC._ipp._tcp.local/cups?uuid=x");
    EXPECT_EQ(name.instance, QString("CUPS-PDF @ jinni-PC"));
}

TEST(ut_AvahiTool, parseDnssdNameIsCaseInsensitiveOnScheme)
{
    EXPECT_TRUE(parseDnssdName("DNSSD://X._ipp._tcp.local/").isValid());
}

TEST(ut_AvahiTool, parseDnssdNameRejectsDegenerateInput)
{
    EXPECT_FALSE(parseDnssdName("").isValid());
    EXPECT_FALSE(parseDnssdName("dnssd://").isValid());
    EXPECT_FALSE(parseDnssdName("dnssd://._ipp._tcp.local/").isValid());   // empty instance
    EXPECT_FALSE(parseDnssdName("dnssd://._ipp._tcp").isValid());
    EXPECT_FALSE(parseDnssdName("dnssd://X._unknown._tcp.local/").isValid());
}

TEST(ut_AvahiTool, isIppUsbUriVariants)
{
    EXPECT_TRUE(isIppUsbUri("ipps://localhost:60001/ipp/print"));
    EXPECT_TRUE(isIppUsbUri("IPP://LOCALHOST:60001/ipp/print"));
    EXPECT_TRUE(isIppUsbUri("ipp://[::1]:61000/ipp/print"));
    EXPECT_TRUE(isIppUsbUri("ipp://127.1.2.3:60000/ipp/print"));
    EXPECT_TRUE(isIppUsbUri("ipp://localhost:65535/"));
}

TEST(ut_AvahiTool, isIppUsbUriRejects)
{
    EXPECT_FALSE(isIppUsbUri(""));
    EXPECT_FALSE(isIppUsbUri("ipp://localhost/ipp/print"));              // no port
    EXPECT_FALSE(isIppUsbUri("ipp://localhost:59999/ipp/print"));        // below the ipp-usb range
    EXPECT_FALSE(isIppUsbUri("http://localhost:60000/"));
    EXPECT_FALSE(isIppUsbUri("ipp://localhost.evil.example:60000/"));
    EXPECT_FALSE(isIppUsbUri("ipp://127.evil.example:60000/"));          // not an address literal
    EXPECT_FALSE(isIppUsbUri("ipp://10.0.0.5:60000/"));
}

TEST(ut_AvahiTool, loopbackAddress)
{
    EXPECT_TRUE(isLoopbackAddress("localhost"));
    EXPECT_TRUE(isLoopbackAddress("LocalHost"));
    EXPECT_TRUE(isLoopbackAddress("127.0.0.1"));
    EXPECT_TRUE(isLoopbackAddress("127.255.0.9"));
    EXPECT_TRUE(isLoopbackAddress("::1"));
    EXPECT_FALSE(isLoopbackAddress(""));
    EXPECT_FALSE(isLoopbackAddress("192.168.0.1"));
    EXPECT_FALSE(isLoopbackAddress("0.0.0.0"));
    EXPECT_FALSE(isLoopbackAddress("127.example.com"));
    EXPECT_FALSE(isLoopbackAddress("fe80::1"));
}

TEST(ut_AvahiTool, serviceRecordLoopbackFollowsAddress)
{
    ServiceRecord record;
    EXPECT_FALSE(record.isLoopback());
    record.address = "127.0.0.1";
    EXPECT_TRUE(record.isLoopback());
    record.address = "192.168.1.5";
    EXPECT_FALSE(record.isLoopback());
}

TEST(ut_AvahiTool, serviceRecordTxtLookupIsCaseInsensitive)
{
    ServiceRecord record;
    record.txt.insert("uuid", "abc");
    EXPECT_EQ(record.txtValue("UUID"), QString("abc"));
    EXPECT_TRUE(record.txtValue("missing").isEmpty());
}

TEST(ut_AvahiTool, serviceRecordIppUsbNeedsLoopbackAndUsbKeys)
{
    ServiceRecord record;
    record.address = "192.168.1.5";
    record.txt.insert("usb_mdl", "X");
    EXPECT_FALSE(record.isIppUsb());   // not loopback

    record.address = "127.0.0.1";
    EXPECT_TRUE(record.isIppUsb());

    record.txt.clear();
    EXPECT_FALSE(record.isIppUsb());   // no usb identity
}

TEST(ut_AvahiTool, modelFromInstanceNameRemovesSerialAndSuffix)
{
    EXPECT_EQ(modelFromInstanceName("Pantum P3301DN Series 6076A2 (USB)"), QString("Pantum P3301DN Series"));
    EXPECT_EQ(modelFromInstanceName("Pantum   P3301DN  Series  6076A2"), QString("Pantum P3301DN Series"));
    EXPECT_EQ(modelFromInstanceName("Foo (USB)"), QString("Foo"));
    EXPECT_EQ(modelFromInstanceName(""), QString());
}

TEST(ut_AvahiTool, modelFromInstanceNameKeepsModelNumbers)
{
    EXPECT_EQ(modelFromInstanceName("Foo 123456"), QString("Foo 123456"));          // digits only
    EXPECT_EQ(modelFromInstanceName("HP LaserJet ABCDEF"), QString("HP LaserJet ABCDEF"));   // letters only
    EXPECT_EQ(modelFromInstanceName("Brother HL-L2350DW"), QString("Brother HL-L2350DW"));
}

TEST(ut_AvahiTool, serviceRecordModelTrimsTxtValue)
{
    ServiceRecord record;
    record.name = "ignored";
    record.txt.insert("ty", "  Some Model  ");
    EXPECT_EQ(record.model(), QString("Some Model"));
    record.txt.insert("ty", "   ");
    EXPECT_EQ(record.model(), QString("ignored"));
}

TEST(ut_AvahiTool, stripTrailingDot)
{
    EXPECT_EQ(stripTrailingDot("local."), QString("local"));
    EXPECT_EQ(stripTrailingDot("local"), QString("local"));
    EXPECT_EQ(stripTrailingDot(""), QString());
    EXPECT_EQ(stripTrailingDot("a.b.."), QString("a.b."));   // only one dot is removed
}

TEST(ut_AvahiTool, hostMatchesRecord)
{
    ServiceRecord record;
    record.host = "printer.local.";
    record.address = "192.168.1.5";
    EXPECT_TRUE(hostMatchesRecord("printer.local", record));
    EXPECT_TRUE(hostMatchesRecord("PRINTER.LOCAL", record));
    EXPECT_TRUE(hostMatchesRecord("192.168.1.5", record));
    EXPECT_FALSE(hostMatchesRecord("other.local", record));
    EXPECT_FALSE(hostMatchesRecord("192.168.1.6", record));
    EXPECT_FALSE(hostMatchesRecord("localhost", record));   // record is not a loopback service
}

TEST(ut_AvahiTool, hostMatchesLoopbackRecordByAnyLoopbackName)
{
    ServiceRecord record;
    record.host = "printer.local";
    record.address = "127.0.0.1";
    EXPECT_TRUE(hostMatchesRecord("localhost", record));
    EXPECT_TRUE(hostMatchesRecord("::1", record));
    EXPECT_TRUE(hostMatchesRecord("127.0.0.1", record));
    EXPECT_FALSE(hostMatchesRecord("192.168.1.5", record));
}

TEST(ut_AvahiTool, pathMatchesRecord)
{
    ServiceRecord record;
    record.txt.insert("rp", "ipp/print");
    EXPECT_TRUE(pathMatchesRecord("/ipp/print", record));
    EXPECT_TRUE(pathMatchesRecord("ipp/print", record));
    EXPECT_TRUE(pathMatchesRecord("", record));
    EXPECT_TRUE(pathMatchesRecord("/", record));
    EXPECT_FALSE(pathMatchesRecord("/other", record));

    ServiceRecord unpublished;
    EXPECT_TRUE(pathMatchesRecord("/anything", unpublished));
}

TEST(ut_AvahiTool, parseTxtRecordIgnoresNonArrayValues)
{
    EXPECT_TRUE(parseTxtRecord(QVariant()).isEmpty());
    EXPECT_TRUE(parseTxtRecord(QVariant(QStringLiteral("rp=ipp/print"))).isEmpty());
}

TEST(ut_AvahiTool, browseCollectorCollectsItems)
{
    BrowseCollector collector(1);
    collector.onItemNew(2, 0, "A", "_ipp._tcp", "local", 0u);
    collector.onItemNew(3, 0, "B", "_ipp._tcp", "local", 0u);
    ASSERT_EQ(collector.items().size(), 2);
    EXPECT_EQ(collector.items().at(0).name, QString("A"));
    EXPECT_EQ(collector.items().at(0).interfaceIndex, 2);
    EXPECT_EQ(collector.items().at(1).interfaceIndex, 3);
    EXPECT_EQ(collector.items().at(1).domain, QString("local"));
}

TEST(ut_AvahiTool, browseCollectorFinishesAfterEveryBrowserIsDone)
{
    BrowseCollector collector(2);
    int finished = 0;
    QObject::connect(&collector, &BrowseCollector::finished, [&finished]() { ++finished; });

    EXPECT_FALSE(collector.isDone());
    collector.onAllForNow();
    EXPECT_FALSE(collector.isDone());
    EXPECT_EQ(finished, 0);
    collector.onFailure("boom");   // a failing browser counts as done
    EXPECT_TRUE(collector.isDone());
    EXPECT_EQ(finished, 1);
}

TEST(ut_AvahiTool, browseCollectorWithoutBrowsersIsDone)
{
    BrowseCollector collector(0);
    EXPECT_TRUE(collector.isDone());
}

TEST(ut_AvahiTool, parseIppUsbStatusTwoDevices)
{
    const QString output =
        "ipp-usb status\n"
        "   1. Bus 001 Device 018  232b:0ef5  \"Pantum P3301DN series\"\n"
        "      status: OK\n"
        "   2. Bus 002 Device 003  03F0:0C17  \"HP LaserJet Pro M102\"\n"
        "      status: Failed to start\n";
    const QList<IppUsbDevice> devices = parseIppUsbStatus(output);
    ASSERT_EQ(devices.size(), 2);
    EXPECT_EQ(devices.at(0).vendorId, QString("232b"));
    EXPECT_EQ(devices.at(0).productId, QString("0ef5"));
    EXPECT_EQ(devices.at(0).model, QString("Pantum P3301DN series"));
    EXPECT_EQ(devices.at(0).status, QString("OK"));
    EXPECT_TRUE(devices.at(0).isOk());
    EXPECT_EQ(devices.at(1).vendorId, QString("03f0"));   // ids are lower-cased
    EXPECT_EQ(devices.at(1).productId, QString("0c17"));
    EXPECT_EQ(devices.at(1).status, QString("Failed to start"));
    EXPECT_FALSE(devices.at(1).isOk());
}

TEST(ut_AvahiTool, parseIppUsbStatusMissingStatusLine)
{
    const QList<IppUsbDevice> last = parseIppUsbStatus("   1. Bus 001 Device 018  232b:0ef5  \"P\"");
    ASSERT_EQ(last.size(), 1);
    EXPECT_TRUE(last.at(0).status.isEmpty());
    EXPECT_FALSE(last.at(0).isOk());

    const QList<IppUsbDevice> unrelated = parseIppUsbStatus(
        "   1. Bus 001 Device 018  232b:0ef5  \"P\"\n   something else\n      status: OK\n");
    ASSERT_EQ(unrelated.size(), 1);
    EXPECT_TRUE(unrelated.at(0).status.isEmpty());   // only the line right after the device counts
}

TEST(ut_AvahiTool, parseIppUsbStatusToleratesCrlfAndCase)
{
    const QList<IppUsbDevice> devices = parseIppUsbStatus(
        "   1. Bus 001 Device 018  232B:0EF5  \"Q\"\r\n      Status: ok\r\n");
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices.at(0).vendorId, QString("232b"));
    EXPECT_EQ(devices.at(0).status, QString("ok"));
    EXPECT_TRUE(devices.at(0).isOk());
}

TEST(ut_AvahiTool, parseIppUsbStatusAllowsEmptyModel)
{
    const QList<IppUsbDevice> devices = parseIppUsbStatus("1. Bus 1 Device 2 abcd:ef01 \"\"\n status: OK\n");
    ASSERT_EQ(devices.size(), 1);
    EXPECT_TRUE(devices.at(0).model.isEmpty());
}

TEST(ut_AvahiTool, parseIppUsbStatusIgnoresGarbage)
{
    EXPECT_TRUE(parseIppUsbStatus("").isEmpty());
    EXPECT_TRUE(parseIppUsbStatus("ipp-usb is not running\n").isEmpty());
    EXPECT_TRUE(parseIppUsbStatus("1. Bus 001 Device 018 zzzz:0ef5 \"P\"\n").isEmpty());   // not hex
    EXPECT_TRUE(parseIppUsbStatus("1. Bus 001 Device 018 232b:0ef5 P\n").isEmpty());          // model not quoted
}

TEST(ut_AvahiTool, dnssdUriFromIppUriRejectsOtherInputWithoutBrowsing)
{
    EXPECT_TRUE(dnssdUriFromIppUri("").isEmpty());
    EXPECT_TRUE(dnssdUriFromIppUri("http://printer.local/ipp/print").isEmpty());
    EXPECT_TRUE(dnssdUriFromIppUri("socket://10.0.0.1:9100").isEmpty());
    EXPECT_TRUE(dnssdUriFromIppUri("ipp:///ipp/print").isEmpty());   // no host and no service name
}

TEST(ut_AvahiTool, resolveDnssdUriWithoutServiceNameIsNotFound)
{
    const DnssdInfo info = resolveDnssdUri("ipp://printer.local/ipp/print");
    EXPECT_FALSE(info.found);
    EXPECT_TRUE(info.ip.isEmpty());
    EXPECT_FALSE(info.isShared);
}

TEST(ut_AvahiTool, resolveDnssdUriServesFreshCacheEntries)
{
    const QString uri = "dnssd://Cached%20Printer._ipp._tcp.local/?uuid=cache-test";
    DnssdInfo stored;
    stored.found = true;
    stored.ip = "192.0.2.7";
    stored.isShared = true;
    storeCache(uri, stored);

    const DnssdInfo info = resolveDnssdUri(uri);
    EXPECT_TRUE(info.found);
    EXPECT_EQ(info.ip, QString("192.0.2.7"));
    EXPECT_TRUE(info.isShared);
}

