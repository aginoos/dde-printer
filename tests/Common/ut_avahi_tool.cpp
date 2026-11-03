// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QFile>
#include <QString>
#include <QTemporaryDir>

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

TEST(ut_AvahiTool, buildDnssdUriEncodesUuidAndChecksType)
{
    EXPECT_EQ(buildDnssdUri("P", "a&b=c d"), QString("dnssd://P._ipp._tcp.local/?uuid=a%26b%3Dc%20d"));
    EXPECT_EQ(buildDnssdUri("P", "u", "_ipp._tcp/../x"), QString("dnssd://P._ipp._tcp.local/?uuid=u"));
    EXPECT_EQ(buildDnssdUri("P", "u", QString()), QString("dnssd://P._ipp._tcp.local/?uuid=u"));
    EXPECT_EQ(buildDnssdUri("P", "u", "_pdl-datastream._tcp"), QString("dnssd://P._pdl-datastream._tcp.local/?uuid=u"));
    EXPECT_EQ(buildDnssdUri("P", "123e4567-e89b-12d3-a456-426614174000"),
              QString("dnssd://P._ipp._tcp.local/?uuid=123e4567-e89b-12d3-a456-426614174000"));
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


namespace {

ServiceRecord makeRecord(const QString &name, const QString &type, const QString &host, const QString &address,
                         quint16 port, int ifIndex = 2, const QMap<QString, QString> &txt = {})
{
    ServiceRecord r;
    r.name = name;
    r.type = type;
    r.domain = "local";
    r.host = host;
    r.address = address;
    r.port = port;
    r.interfaceIndex = ifIndex;
    r.txt = txt;
    return r;
}

// Installs a fake set of published services for the duration of a test and clears the cache.
struct PublishedScope
{
    QList<ServiceRecord> records;
    QList<QStringList> requestedTypes;
    QList<int> requestedInterfaces;
    int calls = 0;

    PublishedScope()
    {
        g_dnssdCache.clear();
        g_publishedServices = [this](const QStringList &types, int ifIndex, int) {
            ++calls;
            requestedTypes.append(types);
            requestedInterfaces.append(ifIndex);
            return records;
        };
    }
    ~PublishedScope()
    {
        g_publishedServices = nullptr;
        g_ippUsbProgramOverride.clear();
        g_dnssdCache.clear();
    }
};

}  // namespace

TEST(ut_AvahiTool, browseServicesKeepsOnlyRequestedTypesAndFilter)
{
    PublishedScope fake;
    fake.records = {makeRecord("A", "_ipp._tcp", "a.local", "10.0.0.1", 631),
                    makeRecord("B", "_ipp._tcp", "b.local", "10.0.0.2", 631),
                    makeRecord("C", "_printer._tcp", "c.local", "10.0.0.3", 515)};
    const QList<ServiceRecord> all = browseServices({"_ipp._tcp"});
    ASSERT_EQ(all.size(), 2);
    const QList<ServiceRecord> onlyB = browseServices({"_ipp._tcp"}, 3000, [](const QString &name, const QString &, const QString &) {
        return name == "B";
    });
    ASSERT_EQ(onlyB.size(), 1);
    EXPECT_EQ(onlyB.first().address, QString("10.0.0.2"));
    EXPECT_TRUE(browseServices({}).isEmpty());
    EXPECT_EQ(fake.calls, 2);   // the empty type list never reaches the bus
}

TEST(ut_AvahiTool, localIppUsbServicesKeepsOnlyUsbBackedLoopbackWithPort)
{
    PublishedScope fake;
    const QNetworkInterface lo = QNetworkInterface::interfaceFromName("lo");
    const int loIndex = lo.isValid() ? lo.index() : -1;
    const QMap<QString, QString> usb{{"usb_mfg", "Pantum"}, {"ty", "Pantum P3301DN"}};
    fake.records = {makeRecord("Pantum P3301DN Series 6076A2 (USB)", "_ipp._tcp", "h", "127.0.0.1", 60000, loIndex, usb),
                    makeRecord("NoPort", "_ipp._tcp", "h", "127.0.0.1", 0, loIndex, usb),
                    makeRecord("Network", "_ipp._tcp", "n", "192.168.1.5", 631, loIndex, usb),
                    makeRecord("Shared", "_ipp._tcp", "h", "127.0.0.1", 631, loIndex, {{"usb_mfg", "x"}, {"printer-type", "0x1"}})};
    const QList<ServiceRecord> usbOnly = localIppUsbServices();
    ASSERT_EQ(usbOnly.size(), 1);
    EXPECT_EQ(usbOnly.first().port, quint16(60000));
    EXPECT_EQ(localIppUsbModelByPort(60000), QString("Pantum P3301DN"));
    EXPECT_TRUE(localIppUsbModelByPort(60001).isEmpty());
    ASSERT_FALSE(fake.requestedTypes.isEmpty());
    EXPECT_EQ(fake.requestedTypes.first(), QStringList{"_ipp._tcp"});
}

TEST(ut_AvahiTool, resolveDnssdPrefersNetworkAddressOverLoopback)
{
    PublishedScope fake;
    fake.records = {makeRecord("Office", "_ipp._tcp", "o.local", "127.0.0.1", 631),
                    makeRecord("Office", "_ipp._tcp", "o.local", "192.168.1.9", 631)};
    const DnssdInfo info = resolveDnssdUri(buildDnssdUri("Office", "u1"));
    EXPECT_TRUE(info.found);
    EXPECT_EQ(info.ip, QString("192.168.1.9"));
    EXPECT_FALSE(info.isShared);
}

TEST(ut_AvahiTool, resolveDnssdFallsBackToLoopbackAndFlagsSharedQueues)
{
    PublishedScope fake;
    fake.records = {makeRecord("Q", "_ipp._tcp", "q", "127.0.0.1", 631, 1, {{"printer-type", "0x809046"}})};
    const DnssdInfo info = resolveDnssdUri(buildDnssdUri("Q", QString()));
    EXPECT_TRUE(info.found);
    EXPECT_EQ(info.ip, QString("127.0.0.1"));
    EXPECT_TRUE(info.isShared);
}

TEST(ut_AvahiTool, resolveDnssdFoundWithoutAddressHasEmptyIp)
{
    PublishedScope fake;
    fake.records = {makeRecord("NoIp", "_ipp._tcp", "n", QString(), 631)};
    const DnssdInfo info = resolveDnssdUri(buildDnssdUri("NoIp", QString()));
    EXPECT_TRUE(info.found);
    EXPECT_TRUE(info.ip.isEmpty());
}

TEST(ut_AvahiTool, resolveDnssdIgnoresOtherInstancesAndDomains)
{
    PublishedScope fake;
    fake.records = {makeRecord("Other", "_ipp._tcp", "o", "10.0.0.8", 631)};
    EXPECT_FALSE(resolveDnssdUri(buildDnssdUri("Wanted", QString())).found);
    ServiceRecord remote = makeRecord("Wanted", "_ipp._tcp", "w", "10.0.0.9", 631);
    remote.domain = "corp.example";
    fake.records = {remote};
    EXPECT_FALSE(resolveDnssdUri(buildDnssdUri("Wanted", QString())).found);   // .local was asked for
}

TEST(ut_AvahiTool, resolveDnssdCachesFoundResultOnly)
{
    PublishedScope fake;
    const QString uri = buildDnssdUri("Cache", QString());
    EXPECT_FALSE(resolveDnssdUri(uri).found);      // nothing published: not cached
    EXPECT_FALSE(resolveDnssdUri(uri).found);
    EXPECT_EQ(fake.calls, 2);

    fake.records = {makeRecord("Cache", "_ipp._tcp", "c", "10.1.1.1", 631)};
    EXPECT_EQ(resolveDnssdUri(uri).ip, QString("10.1.1.1"));
    const int callsAfterFirst = fake.calls;
    fake.records.clear();
    EXPECT_EQ(resolveDnssdUri(uri).ip, QString("10.1.1.1"));   // served from the cache
    EXPECT_EQ(fake.calls, callsAfterFirst);
}

TEST(ut_AvahiTool, dnssdUriFromIppUriMatchesByHostPortAndPath)
{
    PublishedScope fake;
    fake.records = {makeRecord("Lab Printer", "_ipp._tcp", "lab.local.", "10.0.0.5", 631, 2,
                               {{"rp", "ipp/print"}, {"uuid", "abc-123"}})};
    EXPECT_EQ(dnssdUriFromIppUri("ipp://lab.local/ipp/print"), buildDnssdUri("Lab Printer", "abc-123", "_ipp._tcp"));
    EXPECT_EQ(dnssdUriFromIppUri("ipp://10.0.0.5:631/ipp/print"), buildDnssdUri("Lab Printer", "abc-123", "_ipp._tcp"));
    EXPECT_TRUE(dnssdUriFromIppUri("ipp://10.0.0.5:632/ipp/print").isEmpty());    // port differs
    EXPECT_TRUE(dnssdUriFromIppUri("ipp://10.0.0.5/other").isEmpty());            // queue path differs
    EXPECT_TRUE(dnssdUriFromIppUri("ipp://10.0.0.6/ipp/print").isEmpty());        // other host
}

TEST(ut_AvahiTool, dnssdUriFromIppsUriSearchesSecureTypes)
{
    PublishedScope fake;
    fake.records = {makeRecord("Sec", "_ipps._tcp", "s", "10.0.0.7", 443)};
    EXPECT_EQ(dnssdUriFromIppUri("ipps://10.0.0.7:443/"), buildDnssdUri("Sec", QString(), "_ipps._tcp"));
    ASSERT_FALSE(fake.requestedTypes.isEmpty());
    EXPECT_EQ(fake.requestedTypes.first(), (QStringList{"_ipps._tcp", "_ipp-tls._tcp"}));
}

TEST(ut_AvahiTool, dnssdUriFromIppUriByServiceName)
{
    PublishedScope fake;
    fake.records = {makeRecord("Pantum P3301DN", "_ipp._tcp", "x", "127.0.0.1", 60000, 1),
                    makeRecord("Pantum", "_ipp._tcp", "y", "127.0.0.1", 60001, 1)};
    const QString uri = dnssdUriFromIppUri("ipp://Pantum%20P3301DN._ipp._tcp.local/ipp/print");
    EXPECT_EQ(uri, buildDnssdUri("Pantum P3301DN", QString(), "_ipp._tcp"));
}

TEST(ut_AvahiTool, browseOnLoopbackInterfaceFiltersByInterfaceIndex)
{
    PublishedScope fake;
    fake.records = {makeRecord("Lo", "_ipp._tcp", "h", "127.0.0.1", 60000, 1),
                    makeRecord("Eth", "_ipp._tcp", "h", "10.0.0.1", 631, 2)};
    EXPECT_EQ(browseOn({"_ipp._tcp"}, 1, 100, ServiceFilter()).size(), 1);
    EXPECT_EQ(browseOn({"_ipp._tcp"}, -1, 100, ServiceFilter()).size(), 2);
}

TEST(ut_AvahiTool, avahiCallsOnADisconnectedBusFailSoftly)
{
    QDBusConnection dead(QStringLiteral("no-such-connection-for-test"));
    BrowseCollector collector(2);
    const QStringList paths = startBrowsers(dead, {"_ipp._tcp", "_ipps._tcp"}, -1, collector);
    EXPECT_TRUE(paths.isEmpty());
    EXPECT_TRUE(collector.isDone());   // both failures were counted, nobody waits for them

    BrowseCollector::Item item;
    item.name = "X"; item.type = "_ipp._tcp"; item.domain = "local";
    ServiceRecord record;
    EXPECT_FALSE(resolveItem(dead, item, record));
    freeBrowser(dead, "/Client1/ServiceBrowser1");   // must not crash
}

TEST(ut_AvahiTool, waitForBrowsersReturnsAtTimeout)
{
    BrowseCollector collector(1);
    QElapsedTimer t;
    t.start();
    waitForBrowsers(collector, 50);
    EXPECT_FALSE(collector.isDone());
    EXPECT_LT(t.elapsed(), 2000);
    BrowseCollector finished(0);
    waitForBrowsers(finished, 5000);   // already done: returns at once
}

TEST(ut_AvahiTool, ippUsbDevicesParsesFakeProgramOutput)
{
    PublishedScope fake;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString script = dir.filePath("ipp-usb");
    QFile f(script);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("#!/bin/sh\necho '   1. Bus 001 Device 018  232B:0EF5  \"Pantum P3301DN\"'\necho '      status: OK'\n");
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    g_ippUsbProgramOverride = script;
    const QList<IppUsbDevice> devices = ippUsbDevices();
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices.first().vendorId, QString("232b"));
    EXPECT_TRUE(devices.first().isOk());
}

TEST(ut_AvahiTool, ippUsbDevicesEmptyWhenProgramFails)
{
    PublishedScope fake;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString script = dir.filePath("ipp-usb");
    QFile f(script);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("#!/bin/sh\necho '1. Bus 001 Device 018 232b:0ef5 \"P\"'\nexit 3\n");
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    g_ippUsbProgramOverride = script;
    EXPECT_TRUE(ippUsbDevices().isEmpty());
}
