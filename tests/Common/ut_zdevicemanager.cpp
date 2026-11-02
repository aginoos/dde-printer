// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>

// Included (and left out of the build list, see Common.pri) to reach the
// string helpers in the unnamed namespaces and the protected merge logic.
#include "vendor/zdevicemanager.cpp"

namespace {

using DeviceMap = map<string, map<string, string>>;

// attrValueToQString() drops the first character (the CUPS value tag), so
// test values carry a one-character prefix.
map<string, string> backendEntry(const char *cls, const char *info, const char *makeModel,
                                 const char *deviceId = "", const char *location = "")
{
    map<string, string> entry;
    entry[CUPS_DEV_CLASS] = string("s") + cls;
    entry[CUPS_DEV_INFO] = string("s") + info;
    entry[CUPS_DEV_MAKE_MODE] = string("s") + makeModel;
    entry[CUPS_DEV_ID] = string("s") + deviceId;
    entry[CUPS_DEV_LOCATION] = string("s") + location;
    return entry;
}

}  // namespace

// ---------------------------------------------------------- lpdUriWithHost

TEST(ut_ZDeviceManager, lpdUriKeepsNonLpdUris)
{
    EXPECT_EQ(lpdUriWithHost("socket://printer-name:9100", "10.0.0.5"), QString("socket://printer-name:9100"));
    EXPECT_EQ(lpdUriWithHost("ipp://printer-name/ipp/print", "10.0.0.5"), QString("ipp://printer-name/ipp/print"));
}

TEST(ut_ZDeviceManager, lpdUriKeepsUriThatAlreadyUsesTheHost)
{
    EXPECT_EQ(lpdUriWithHost("lpd://10.0.0.5/queue", "10.0.0.5"), QString("lpd://10.0.0.5/queue"));
    EXPECT_EQ(lpdUriWithHost("lpd://Printer.Local/queue", "printer.local"), QString("lpd://Printer.Local/queue"));
}

TEST(ut_ZDeviceManager, lpdUriReplacesUnresolvableHost)
{
    EXPECT_EQ(lpdUriWithHost("lpd://printer-name/queue", "10.0.0.5"), QString("lpd://10.0.0.5/queue"));
    EXPECT_EQ(lpdUriWithHost("lpd://user@printer-name:515/queue", "10.0.0.5"),
              QString("lpd://user@10.0.0.5:515/queue"));
}

// ------------------------------------------------------ modelFromHplipUri

TEST(ut_ZDeviceManager, hplipModelIsReadFromTheNetPath)
{
    EXPECT_EQ(modelFromHplipUri("hp:/net/HP_ColorLaserJet_M253-M254?ip=10.20.255.24"),
              QString("HP ColorLaserJet M253-M254"));
    EXPECT_EQ(modelFromHplipUri("hp:/net/HP%20LaserJet%20M1?ip=10.0.0.1"), QString("HP LaserJet M1"));
    EXPECT_EQ(modelFromHplipUri("hp:/net/_X_"), QString("X"));
}

TEST(ut_ZDeviceManager, hplipModelRejectsOtherUris)
{
    EXPECT_TRUE(modelFromHplipUri("hp:/usb/HP_LaserJet?serial=X").isEmpty());
    EXPECT_TRUE(modelFromHplipUri("hp:/net/").isEmpty());
    EXPECT_TRUE(modelFromHplipUri("hp:/net/?ip=10.0.0.1").isEmpty());
    EXPECT_TRUE(modelFromHplipUri("socket://10.0.0.1:9100").isEmpty());
    EXPECT_TRUE(modelFromHplipUri("").isEmpty());
}

// ------------------------------------------------------- isValidProbeHost

TEST(ut_ZDeviceManager, probeHostAccepts)
{
    EXPECT_TRUE(isValidProbeHost("10.0.0.1"));
    EXPECT_TRUE(isValidProbeHost("printer.local"));
    EXPECT_TRUE(isValidProbeHost("host_name-1"));
    EXPECT_TRUE(isValidProbeHost("fe80::1"));
    EXPECT_TRUE(isValidProbeHost("fe80::1%eth0"));
    EXPECT_TRUE(isValidProbeHost("[fe80::1]"));
    EXPECT_TRUE(isValidProbeHost(QString(253, QLatin1Char('a'))));
}

TEST(ut_ZDeviceManager, probeHostRejects)
{
    EXPECT_FALSE(isValidProbeHost(""));
    EXPECT_FALSE(isValidProbeHost(QString(254, QLatin1Char('a'))));
    EXPECT_FALSE(isValidProbeHost("a b"));
    EXPECT_FALSE(isValidProbeHost("a;b"));
    EXPECT_FALSE(isValidProbeHost("$(id)"));
    EXPECT_FALSE(isValidProbeHost("a/b"));
    EXPECT_FALSE(isValidProbeHost("a'b"));
    EXPECT_FALSE(isValidProbeHost("host\n"));
    EXPECT_FALSE(isValidProbeHost(QString::fromUtf8("\xe6\x89\x93\xe5\x8d\xb0")));
}

// -------------------------------------------------------- ippHostForUri

TEST(ut_ZDeviceManager, ippHostForUriBracketsBareIpv6)
{
    EXPECT_EQ(ippHostForUri("fe80::1"), QString("[fe80::1]"));
    EXPECT_EQ(ippHostForUri("[fe80::1]"), QString("[fe80::1]"));
    EXPECT_EQ(ippHostForUri("10.0.0.1"), QString("10.0.0.1"));
    EXPECT_EQ(ippHostForUri("printer.local"), QString("printer.local"));
    EXPECT_EQ(ippHostForUri(""), QString(""));
}

TEST(ut_ZDeviceManager, ippEndpointsStartWithSlash)
{
    ASSERT_GT(sizeof(kIppPaths) / sizeof(kIppPaths[0]), 1u);
    EXPECT_STREQ(kIppPaths[0], "/ipp/print");
    for (const char *path : kIppPaths) {
        EXPECT_EQ(path[0], '/');
    }
}

// ------------------------------------------------------ parsePjlIdentity

TEST(ut_ZDeviceManager, pjlIdentityFromQuotedAnswer)
{
    EXPECT_EQ(parsePjlIdentity("@PJL INFO ID\r\n\"HP LaserJet M404\"\r\n\f"), QString("HP LaserJet M404"));
    EXPECT_EQ(parsePjlIdentity("\"HP LaserJet M404\"\n"), QString("HP LaserJet M404"));
}

TEST(ut_ZDeviceManager, pjlIdentityOnTheEchoLine)
{
    EXPECT_EQ(parsePjlIdentity("@PJL INFO ID \"HP 1\"\r\n"), QString("HP 1"));
    EXPECT_EQ(parsePjlIdentity("@pjl info id\r\n\"x\"\r\n"), QString("x"));
}

TEST(ut_ZDeviceManager, pjlIdentityIgnoresOtherPjlLines)
{
    EXPECT_EQ(parsePjlIdentity("@PJL ECHO hello\r\n\"Model\"\r\n"), QString("Model"));
}

TEST(ut_ZDeviceManager, pjlIdentityAcceptsUnquotedAnswer)
{
    EXPECT_EQ(parsePjlIdentity("HP LaserJet\r\n"), QString("HP LaserJet"));
}

TEST(ut_ZDeviceManager, pjlIdentityRejectsErrorsAndEmptyAnswers)
{
    EXPECT_TRUE(parsePjlIdentity("").isEmpty());
    EXPECT_TRUE(parsePjlIdentity("\r\n\r\n\f").isEmpty());
    EXPECT_TRUE(parsePjlIdentity("@PJL INFO ID\r\n").isEmpty());
    EXPECT_TRUE(parsePjlIdentity("@PJL INFO ID\r\n\"SYNTAX ERROR\"\r\n").isEmpty());
    EXPECT_TRUE(parsePjlIdentity("@PJL INFO ID\r\n\"  \"\r\n").isEmpty());
}

// ------------------------------------------------ deviceFromIppResponse

TEST(ut_ZDeviceManager, ippResponseFillsTheDevice)
{
    IppPtr response(ippNew());
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", nullptr, "Office");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", nullptr, "Office Laser");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-make-and-model", nullptr, "Generic IPP");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-location", nullptr, "2F");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "device-id", nullptr, "MFG:X;MDL:Y;");
    const char *uris[] = {"ipp://10.0.0.1/ipp/print", "ipps://10.0.0.1/ipp/print"};
    ippAddStrings(response.get(), IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uri-supported", 2, nullptr, uris);

    const TDeviceInfo info = deviceFromIppResponse(response.get(), "ipp://10.0.0.1/ipp/print");
    EXPECT_EQ(info.iType, (int)InfoFrom_Host);
    EXPECT_EQ(info.strClass, QString("network"));
    EXPECT_EQ(info.strName, QString("Office"));
    EXPECT_EQ(info.strInfo, QString("Office Laser"));
    EXPECT_EQ(info.strLocation, QString("2F"));
    EXPECT_EQ(info.strDeviceId, QString("MFG:X;MDL:Y;"));
    // printer-info is the better model source than make-and-model
    EXPECT_EQ(info.strMakeAndModel, QString("Office Laser"));
    // the probed URI first, supported ones appended once
    EXPECT_EQ(info.uriList, (QStringList{"ipp://10.0.0.1/ipp/print", "ipps://10.0.0.1/ipp/print"}));
}

TEST(ut_ZDeviceManager, ippResponseFallsBackToMakeAndModel)
{
    IppPtr response(ippNew());
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-make-and-model", nullptr, "Generic IPP");

    const TDeviceInfo info = deviceFromIppResponse(response.get(), "ipp://h/ipp");
    EXPECT_EQ(info.strMakeAndModel, QString("Generic IPP"));
    EXPECT_EQ(info.strName, QString("Generic IPP"));
    EXPECT_EQ(info.strInfo, QString("Generic IPP"));
}

TEST(ut_ZDeviceManager, ippResponseIgnoresAttributesOutsideThePrinterGroup)
{
    IppPtr response(ippNew());
    ippAddString(response.get(), IPP_TAG_OPERATION, IPP_TAG_NAME, "printer-name", nullptr, "Injected");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", nullptr, "Real");

    const TDeviceInfo info = deviceFromIppResponse(response.get(), "ipp://h/ipp");
    EXPECT_EQ(info.strName, QString("Real"));
}

TEST(ut_ZDeviceManager, ippResponseWithoutAttributesStillReturnsTheUri)
{
    IppPtr response(ippNew());
    const TDeviceInfo info = deviceFromIppResponse(response.get(), "ipp://h/ipp");
    EXPECT_EQ(info.uriList, QStringList{"ipp://h/ipp"});
    EXPECT_TRUE(info.strName.isEmpty());
    EXPECT_TRUE(info.strMakeAndModel.isEmpty());
}

// ------------------------------------------------- RefreshDevicesTask

TEST(ut_ZDeviceManager, taskStoresAndClearsDevices)
{
    RefreshDevicesByBackendTask task;
    int updates = 0;
    QObject::connect(&task, &TaskInterface::signalStatus, [&updates](int, int state) {
        if (state == TStat_Update) {
            ++updates;
        }
    });

    EXPECT_TRUE(task.getResult().isEmpty());
    TDeviceInfo device;
    device.uriList << "socket://10.0.0.1";
    EXPECT_EQ(task.addDevice(device), 0);
    EXPECT_EQ(task.addDevice(device), 0);   // addDevice itself does not de-duplicate
    EXPECT_EQ(task.getResult().size(), 2);
    EXPECT_EQ(updates, 2);

    task.clearDevices();
    EXPECT_TRUE(task.getResult().isEmpty());
}

// ------------------------------------------------ backend device merging

TEST(ut_ZDeviceManager, addDevicesBuildsDeviceFromBackendEntry)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["socket://10.0.0.1:9100"] = backendEntry("network", "Office", "HP LaserJet M404", "MFG:HP;MDL:M404;", "2F");
    task.addDevices(devs, "snmp");

    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 1);
    const TDeviceInfo &info = result.first();
    EXPECT_EQ(info.uriList, QStringList{"socket://10.0.0.1:9100"});
    EXPECT_EQ(info.strClass, QString("network"));
    EXPECT_EQ(info.strInfo, QString("Office"));
    EXPECT_EQ(info.strMakeAndModel, QString("HP LaserJet M404"));
    EXPECT_EQ(info.strName, QString("HP LaserJet M404"));   // make-and-model keeps the manufacturer
    EXPECT_EQ(info.strDeviceId, QString("MFG:HP;MDL:M404;"));
    EXPECT_EQ(info.strLocation, QString("2F"));
    EXPECT_EQ(info.iType, (int)InfoFrom_Detect);
}

TEST(ut_ZDeviceManager, addDevicesNamesDeviceByInfoWithoutMakeAndModel)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["socket://10.0.0.1:9100"] = backendEntry("network", "Office", "");
    task.addDevices(devs, nullptr);

    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().strName, QString("Office"));
}

TEST(ut_ZDeviceManager, addDevicesSkipsUnusableEntries)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["no-colon-here"] = backendEntry("network", "a", "b");
    devs["dnssd://Shared._ipp._tcp.local/"] = backendEntry("network", "a", "b");
    devs["socket://[fe80::1]:9100"] = backendEntry("network", "a", "b");
    devs["lpd://no-such-host.invalid/q"] = backendEntry("network", "a", "b");
    task.addDevices(devs, nullptr);

    EXPECT_TRUE(task.getResult().isEmpty());
}

TEST(ut_ZDeviceManager, addDevicesKeepsLpdDeviceWithResolvableHost)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["lpd://127.0.0.1/queue"] = backendEntry("network", "Local", "Some Printer");
    task.addDevices(devs, nullptr);

    EXPECT_EQ(task.getResult().size(), 1);
}

TEST(ut_ZDeviceManager, mergeDeviceDropsSecondEntryWithTheSameUri)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["socket://10.0.0.1:9100"] = backendEntry("network", "First", "A");
    task.addDevices(devs, "snmp");
    devs["socket://10.0.0.1:9100"] = backendEntry("network", "Second", "B");
    task.addDevices(devs, "dnssd-other");

    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().strInfo, QString("First"));
}

TEST(ut_ZDeviceManager, mergeDeviceJoinsHplipAndUsbEntriesOfOnePrinter)
{
    RefreshDevicesByBackendTask task;
    DeviceMap usb;
    usb["usb://HP/LaserJet?serial=ABC123"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    task.addDevices(usb, "usb");
    DeviceMap hp;
    hp["hp:/usb/LaserJet?serial=abc123"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    task.addDevices(hp, "hp");

    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.first().uriList.size(), 2);
    EXPECT_TRUE(result.first().uriList.contains("usb://HP/LaserJet?serial=ABC123"));
    EXPECT_TRUE(result.first().uriList.contains("hp:/usb/LaserJet?serial=abc123"));
}

TEST(ut_ZDeviceManager, mergeDeviceKeepsSameBackendEntriesApart)
{
    // printer and fax of one device share the serial but are different queues
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["usb://HP/LaserJet?serial=ABC123"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    devs["usb://HP/Fax?serial=ABC123"] = backendEntry("direct", "HP Fax", "HP Fax");
    task.addDevices(devs, "usb");

    EXPECT_EQ(task.getResult().size(), 2);
}

TEST(ut_ZDeviceManager, mergeDeviceKeepsDifferentSerialsAndClassesApart)
{
    RefreshDevicesByBackendTask task;
    DeviceMap usb;
    usb["usb://HP/LaserJet?serial=ABC123"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    task.addDevices(usb, "usb");
    DeviceMap otherSerial;
    otherSerial["hp:/usb/LaserJet?serial=ZZZ999"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    task.addDevices(otherSerial, "hp");
    DeviceMap otherClass;
    otherClass["hp:/usb/LaserJet?serial=ABC123&x=1"] = backendEntry("network", "HP LaserJet", "HP LaserJet");
    task.addDevices(otherClass, "hp");

    EXPECT_EQ(task.getResult().size(), 3);
}

TEST(ut_ZDeviceManager, mergeDeviceSamsungBackendReplacesIppUriOfSameDevice)
{
    RefreshDevicesByBackendTask task;
    DeviceMap ipp;
    ipp["ipp://10.0.0.7/ipp/print"] = backendEntry("network", "Samsung M2020", "Samsung M2020");
    task.addDevices(ipp, "snmp");

    DeviceMap samsung;
    samsung["smfpnetdiscovery://10.0.0.7"] = backendEntry("network", "Samsung M2020", "Samsung M2020");
    task.addDevices(samsung, "smfpnetdiscovery");

    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 1);   // no second entry for the same device
    EXPECT_EQ(result.first().uriList, QStringList{"smfpnetdiscovery://10.0.0.7"});
}

TEST(ut_ZDeviceManager, mergeDeviceSamsungBackendKeepsNonIppUri)
{
    RefreshDevicesByBackendTask task;
    DeviceMap socket;
    socket["socket://10.0.0.7:9100"] = backendEntry("network", "Samsung M2020", "Samsung M2020");
    task.addDevices(socket, "snmp");

    DeviceMap samsung;
    samsung["smfpnetdiscovery://10.0.0.7"] = backendEntry("network", "Samsung M2020", "Samsung M2020");
    task.addDevices(samsung, "smfpnetdiscovery");

    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.first().uriList, QStringList{"socket://10.0.0.7:9100"});
}
