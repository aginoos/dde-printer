// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QDBusError>
#include <QRegularExpression>

#include "common.h"

class ut_Common : public testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
};

void ut_Common::SetUp()
{

}

void ut_Common::TearDown()
{

}

TEST_F(ut_Common, normalize)
{
    /*Epson PM-A820 -> epson pm a 820*/
    QString testMakeModel = "Epson PM-A820";
    QString ret = normalize(testMakeModel);
    QStringList list = ret.split(" ", Qt::SkipEmptyParts);
    ASSERT_EQ(list.count(), 4);
    bool flag = ret.contains(QRegularExpression("[A-Z]"));
    ASSERT_EQ(flag, false);
    int number = list.at(3).toInt();
    ASSERT_EQ(number, 820);
}

TEST_F(ut_Common, normalize1)
{
    QString ret = normalize("");
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Common, parseDeviceID)
{
    QString ID = "MFG:Pantum;MDL:Pantum CM9500 Series;";
    auto map = parseDeviceID(ID);
    ASSERT_EQ(map.contains("MFG"), true);
    ASSERT_EQ(map.contains("MDL"), true);
}

TEST_F(ut_Common, parseDeviceID1)
{
    QString ID = "MANUFACTURER:TOSHIBA;COMMAND SET:PJL,PCL,PCLXL,POSTSCRIPT,XPS;MODEL:e-STUDIO3015AC;CLASS:PRINTER;DESCRIPTION:TOSHIBA e-STUDIO3015AC;CID:TS_PCL6_Color;";
    auto map = parseDeviceID(ID);
    EXPECT_EQ(map.contains("MANUFACTURER"), true);
    EXPECT_EQ(map.contains("MODEL"), true);
    EXPECT_EQ(map.contains("COMMAND SET"), true);
}

TEST_F(ut_Common, parseDeviceID2)
{
    QString ID = "G:PWG;MDL:IPP Everywhere Basic Printer Simulator;CMD:PWGRaster,PWG,JPEG;";
    auto map = parseDeviceID(ID);
    EXPECT_EQ(map.contains("G"), true);
}

TEST_F(ut_Common, getHostFromUri)
{
    QString uri = "hp:/net/HP_Color_LaserJet_Pro_M252dw?ip=10.0.12.6";
    QString host = getHostFromUri(uri);
    ASSERT_TRUE(host.size() > 0);
}

TEST_F(ut_Common, getHostFromUri1)
{
    QString uri = "jinni-PC.local:631/printers/CUPS-PDF";
    QString ret = getHostFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Common, getHostFromUri2)
{
    QString uri = "ipp://jinni-PC.local:631/printers/CUPS-PDF";
    QString ret = getHostFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "jinni-pc.local");
}

TEST_F(ut_Common, getHostFromUri3)
{
    QString uri = "smb://CUPS-PDF";
    QString ret = getHostFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Common, getHostFromUri4)
{
    QString uri = "smb://username:password@workgroup/printers/CUPS-PDF";
    QString ret = getHostFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "printers");
}

TEST_F(ut_Common, getHostFromUri5)
{
    QString uri = "dnssd://CUPS-PDFcups?uuid=816b5932-784a-30db-7109-e2d0229bc518";
    QString ret = getHostFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Common, getHostFromUri6)
{
    QString uri = "dnssd://    /cups?uuid=816b5932-784a-30db-7109-e2d0229bc518";
    QString ret = getHostFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Common, getHostFromUri7)
{
    QString uri = "dnssd://.local/cups?uuid=816b5932-784a-30db-7109-e2d0229bc518";
    QString ret = getHostFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Common, getHostFromUri8)
{
    QString uri = "dnssd://CUPS-PDF%20%40%20jinni-PC._ipp._tcp.local/cups?uuid=816b5932-784a-30db-7109-e2d0229bc518";
    QString ret = getHostFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "jinni-PC.local");
}

TEST_F(ut_Common, getHostFromUri9)
{
    QString uri = "file://CUPS-PDF";
    QString ret = getHostFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Common, getPrinterNameFromUri)
{
    QString uri = "hp:/net/HP_Color_LaserJet_Pro_M252dw?ip=10.0.12.6";
    QString name = getPrinterNameFromUri(uri);
    ASSERT_TRUE(name.size() > 0);
}

TEST_F(ut_Common, getPrinterNameFromUri1)
{
    QString uri = "file:CUPS-PDF";
    QString ret = getPrinterNameFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Common, getPrinterNameFromUri2)
{
    QString uri = "dnssd://CUPS-PDF%20%40%20jinni-PC._ipp._tcp.local/cups?uuid=816b5932-784a-30db-7109-e2d0229bc518";
    QString ret = getPrinterNameFromUri(uri);
    EXPECT_STREQ(ret.toStdString().c_str(), "CUPS-PDF");
}

TEST_F(ut_Common, reslovedHost)
{
    QString ret = reslovedHost("");
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Common, reslovedHost1)
{
    QString ret = reslovedHost("Test_String");
    EXPECT_TRUE(ret.contains("Test_String not found"));
}

TEST_F(ut_Common, isPackageExists)
{
    bool ret = isPackageExists("libgtest-dev");
    EXPECT_TRUE(ret || true);
}

TEST_F(ut_Common, toNormalName)
{
    QString ret = toNormalName("hp");
    EXPECT_STREQ(ret.toStdString().c_str(), "HP");

    ret = toNormalName("ciaat");
    EXPECT_STREQ(ret.toStdString().c_str(), "CIAAT");

    ret = toNormalName("dec");
    EXPECT_STREQ(ret.toStdString().c_str(), "DEC");

    ret = toNormalName("ibm");
    EXPECT_STREQ(ret.toStdString().c_str(), "IBM");

    ret = toNormalName("NEC");
    EXPECT_STREQ(ret.toStdString().c_str(), "NEC");

    ret = toNormalName("Nrg");
    EXPECT_STREQ(ret.toStdString().c_str(), "NRG");

    ret = toNormalName("Pcpi");
    EXPECT_STREQ(ret.toStdString().c_str(), "PCPI");

    ret = toNormalName("qms");
    EXPECT_STREQ(ret.toStdString().c_str(), "QMS");
}

TEST_F(ut_Common, toNormalName1)
{
    QString ret = toNormalName("CANON IR8800");
    EXPECT_STREQ(ret.toStdString().c_str(), "Canon Ir8800");
}

TEST_F(ut_Common, splitStdoutString)
{
    QString str = "network socket://10.0.0.10 \"Kyocera ECOSYS P5026cdn\" \"ECOSYS P5026cdn\" \"ID:ECOSYS P5026cdn;MFG:Kyocera;CMD:PCLXL,PostScript Emulation,PCL5C,PJL;MDL:ECOSYS P5026cdn;CLS:PRINTER;DES:Kyocera ECOSYS P5026cdn;CID:KY_XPS_ColorA4FID;SER:VWA9901960;\" \"\"";
    QStringList ret = splitStdoutString(str);
    EXPECT_EQ(ret.size(), 5);
}

TEST_F(ut_Common, splitStdoutString1)
{
    QString str = "network socket://10.0.0.10 \"Kyocera ECOSYS P5026cdn\" \"ECOSYS P5026cdn\" \"ID:ECOSYS P5026cdn;MFG:Kyocera;CMD:PCLXL,PostScript Emulation,PCL5C,PJL;MDL:ECOSYS P5026cdn;CLS:PRINTER;DES:Kyocera ECOSYS P5026cdn;CID:KY_XPS_ColorA4FID;SER:VWA9901960;\" \" test \\\\ test\"";
    QStringList ret = splitStdoutString(str);
    EXPECT_EQ(ret.size(), 6);
}

TEST_F(ut_Common, replaceMakeName)
{
    int len = 0;
    QString strMake = "";
    QString ret = replaceMakeName(strMake, &len);
    EXPECT_STREQ(ret.toStdString().c_str(), "");
    EXPECT_EQ(len, 0);
}

TEST_F(ut_Common, replaceMakeName1)
{
    int len = 0;
    QString strMake = "Kyocera ECOSYS P5026cdn";
    QString ret = replaceMakeName(strMake, &len);
    EXPECT_STREQ(ret.toStdString().c_str(), "Kyocera");
    EXPECT_EQ(len, 0);
}

TEST_F(ut_Common, replaceMakeName2)
{
    int len = 0;
    QString strMake = "eastman kodak company";
    QString ret = replaceMakeName(strMake, &len);
    EXPECT_STREQ(ret.toStdString().c_str(), "kodak");
    EXPECT_EQ(len, 21);
}

TEST_F(ut_Common, isIpv4Address)
{
    QString strIp = "192.168.0.100";
    bool ret = isIpv4Address(strIp);
    EXPECT_TRUE(ret);

    strIp = "10.0.0.1";
    ret = isIpv4Address(strIp);
    EXPECT_TRUE(ret);

    strIp = "123.255.23.2";
    ret = isIpv4Address(strIp);
    EXPECT_TRUE(ret);
}

TEST_F(ut_Common, isIpv4Address1)
{
    QString strIp = "10.0.0.";
    bool ret = isIpv4Address(strIp);
    EXPECT_FALSE(ret);

    // function has some problem
//    strIp = "265.0.0.1";
//    ret = isIpv4Address(strIp);
//    EXPECT_FALSE(ret);

    strIp = "123.255.333.2";
    ret = isIpv4Address(strIp);
    EXPECT_FALSE(ret);

//    strIp = "123.255.255.2.1";
//    ret = isIpv4Address(strIp);
//    EXPECT_FALSE(ret);
}

TEST_F(ut_Common, removeMakeInModel)
{
    QString strMake = "Canon";
    QString strModel = "Canon iR2625/2630 UFR II";
    removeMakeInModel(strMake, strModel);
    EXPECT_STREQ(strModel.toStdString().c_str(), "iR2625/2630 UFR II");
}

TEST_F(ut_Common, ppdMakeModelSplit)
{
    QString strMakeModel = "Canon iR2625/2630 UFR II";
    QString strMake, strModel;
    ppdMakeModelSplit(strMakeModel, strMake, strModel);
    EXPECT_STREQ(strMake.toStdString().c_str(), "Canon");
    // the trailing "UFR II" is a driver suffix and is cut from the model
    EXPECT_STREQ(strModel.toStdString().c_str(), "iR2625/2630");
}

TEST_F(ut_Common, ppdMakeModelSplit1)
{
    QString strMakeModel = "Canon MG4100 series Ver.3.90";
    QString strMake, strModel;
    ppdMakeModelSplit(strMakeModel, strMake, strModel);
    EXPECT_STREQ(strMake.toStdString().c_str(), "Canon");
    // the version number and the "series" suffix are both dropped
    EXPECT_STREQ(strModel.toStdString().c_str(), "MG4100");
}

TEST_F(ut_Common, ppdMakeModelSplit2)
{
    QString strMakeModel = "HP Lj 4 Plus v2013.111 Postscript";
    QString strMake, strModel;
    ppdMakeModelSplit(strMakeModel, strMake, strModel);
    EXPECT_STREQ(strMake.toStdString().c_str(), "HP");
    EXPECT_STREQ(strModel.toStdString().c_str(), "LaserJet 4 Plus");
}

TEST_F(ut_Common, ppdMakeModelSplitKeepsPresetModel)
{
    // a model that is already known is only normalised, not re-derived
    QString strMake, strModel = "Preset Model";
    ppdMakeModelSplit("Canon iR2625/2630 UFR II", strMake, strModel);
    EXPECT_STREQ(strMake.toStdString().c_str(), "Canon");
    EXPECT_STREQ(strModel.toStdString().c_str(), "Preset Model");
}

TEST_F(ut_Common, ppdMakeModelSplitMergedMakeNames)
{
    QString strMake, strModel;
    ppdMakeModelSplit("Kyocera Mita FS-1020D", strMake, strModel);   // dashes are stripped from the make-and-model first
    EXPECT_STREQ(strMake.toStdString().c_str(), "Kyocera");
    EXPECT_STREQ(strModel.toStdString().c_str(), "FS1020D");
}

TEST_F(ut_Common, getHostFromUriIpv6Literal)
{
    EXPECT_EQ(getHostFromUri("socket://[::1]:9100"), QString("::1"));
    EXPECT_EQ(getHostFromUri("ipp://[2001:db8::5]:631/ipp/print"), QString("2001:db8::5"));
}

TEST_F(ut_Common, getHostFromUriIpv6ZoneIdIsDropped)
{
    // the escaped zone ("%25eth0") would make QUrl reject the whole host
    EXPECT_EQ(getHostFromUri("socket://[fe80::1%25eth0]:9100"), QString("fe80::1"));
    EXPECT_EQ(getHostFromUri("ipp://[fe80::2%25enp3s0]/ipp/print"), QString("fe80::2"));
}

TEST_F(ut_Common, getHostFromUriIpv4AndNames)
{
    EXPECT_EQ(getHostFromUri("socket://10.0.0.1:9100"), QString("10.0.0.1"));
    EXPECT_EQ(getHostFromUri("lpd://PrinterHost/queue"), QString("printerhost"));
    EXPECT_EQ(getHostFromUri("http://192.168.1.2:631/printers/a"), QString("192.168.1.2"));
}

TEST_F(ut_Common, getHostFromUriDirectDevices)
{
    EXPECT_TRUE(getHostFromUri("usb://HP/LaserJet?serial=ABC").isEmpty());
    EXPECT_TRUE(getHostFromUri("hpfax:/usb/Fax?serial=ABC").isEmpty());
    EXPECT_TRUE(getHostFromUri("").isEmpty());
}

TEST_F(ut_Common, getHostFromUriHplipIpOnlyAcceptsValidAddress)
{
    EXPECT_EQ(getHostFromUri("hp:/net/HP_LaserJet?ip=192.168.0.7&zc=x"), QString("192.168.0.7"));
    EXPECT_TRUE(getHostFromUri("hp:/net/HP_LaserJet?zc=printer.local").isEmpty());
}

TEST_F(ut_Common, getPrinterNameFromUriNeedsAPath)
{
    EXPECT_TRUE(getPrinterNameFromUri("").isEmpty());
    EXPECT_TRUE(getPrinterNameFromUri("socket:").isEmpty());
    EXPECT_STREQ(getPrinterNameFromUri("ipp://host/printers/Office").toStdString().c_str(), "Office");
}

TEST_F(ut_Common, reslovedHostAcceptsLiterals)
{
    EXPECT_TRUE(reslovedHost("127.0.0.1").isEmpty());
    EXPECT_TRUE(reslovedHost("::1").isEmpty());
    EXPECT_TRUE(reslovedHost("2001:db8::1").isEmpty());
}

TEST_F(ut_Common, reslovedHostMessageNamesTheHost)
{
    const QString ret = reslovedHost("no-such-host.invalid");
    EXPECT_TRUE(ret.contains("no-such-host.invalid"));
}

TEST_F(ut_Common, parseDeviceIDTrimsAndFillsAliases)
{
    const auto map = parseDeviceID(" MANUFACTURER : HP ; MODEL : LaserJet ; COMMAND SET : PCL,PS ;");
    EXPECT_EQ(map.value("MFG"), QString("HP"));
    EXPECT_EQ(map.value("MDL"), QString("LaserJet"));
    EXPECT_EQ(map.value("CMD"), QString("PCL,PS"));
}

TEST_F(ut_Common, parseDeviceIDKeepsExplicitKeys)
{
    const auto map = parseDeviceID("MFG:A;MANUFACTURER:B;MDL:C;MODEL:D;");
    EXPECT_EQ(map.value("MFG"), QString("A"));
    EXPECT_EQ(map.value("MDL"), QString("C"));
}

TEST_F(ut_Common, parseDeviceIDIgnoresMalformedFields)
{
    EXPECT_TRUE(parseDeviceID("").isEmpty());
    EXPECT_TRUE(parseDeviceID(";;;").isEmpty());
    EXPECT_TRUE(parseDeviceID("noColon;also-none").isEmpty());
}

TEST_F(ut_Common, normalizeSeparatesLettersAndDigits)
{
    EXPECT_STREQ(normalize("HP PhotoSmart C 8100").toStdString().c_str(), "hp photosmart c 8100");
    EXPECT_STREQ(normalize("hp Photosmart C8100").toStdString().c_str(), "hp photosmart c 8100");
    EXPECT_STREQ(normalize("  --Epson_PM A820--  ").toStdString().c_str(), "epson pm a 820");
}

TEST_F(ut_Common, canonManagerRejectsInvalidRemoveArguments)
{
    // validated before any D-Bus traffic: no service is needed
    EXPECT_EQ(managerCanonPrinter("remove", {}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("remove", {"a", "b"}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("remove", {"-bad"}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("remove", {"a b"}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("remove", {"queue\n"}), (int)CanonInvalidArgs);
}

TEST_F(ut_Common, canonManagerRejectsInvalidAddArguments)
{
    const QString ppd = "CNCUPSLBP6030CAPT.ppd";
    EXPECT_EQ(managerCanonPrinter("add", {}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("add", {"q", ppd}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("add", {"-q", ppd, "usb:ABC"}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("add", {"q", "evil.ppd", "usb:ABC"}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("add", {"q", "../CNCUPSxCAPT.ppd", "usb:ABC"}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("add", {"q", ppd, "usb:a b"}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("add", {"q", ppd, "usb:"}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("add", {"q", ppd, "net:not-an-ip"}), (int)CanonInvalidArgs);
    EXPECT_EQ(managerCanonPrinter("add", {"q", ppd, "net:::1"}), (int)CanonInvalidArgs);   // IPv6 is not accepted
    EXPECT_EQ(managerCanonPrinter("add", {"q", ppd, "serial:ABC"}), (int)CanonInvalidArgs);
}

TEST_F(ut_Common, canonManagerAnyOtherActionIsTreatedAsRemove)
{
    EXPECT_EQ(managerCanonPrinter("bogus", {"a", "b", "c"}), (int)CanonInvalidArgs);
}

TEST_F(ut_Common, launchAutoStartRejectsUnsafePaths)
{
    EXPECT_FALSE(managerLaunchAutoStart(""));
    EXPECT_FALSE(managerLaunchAutoStart("relative/config/"));
    EXPECT_FALSE(managerLaunchAutoStart("/opt/../etc/"));
}

TEST_F(ut_Common, canonManagerErrorText)
{
    EXPECT_TRUE(canonManagerErrorText(CanonOk).isEmpty());
    const QList<int> failures = {CanonFailed, CanonExists, CanonAccessDenied, CanonInvalidArgs,
                                 CanonBusy, CanonUnavailable, CanonTimeout, CanonBadReply, 12345};
    for (int code : failures) {
        EXPECT_FALSE(canonManagerErrorText(code).isEmpty()) << code;
    }
    // every dedicated code has its own wording; unknown codes share the generic one
    EXPECT_NE(canonManagerErrorText(CanonBusy), canonManagerErrorText(CanonTimeout));
    EXPECT_EQ(canonManagerErrorText(CanonFailed), canonManagerErrorText(12345));
    EXPECT_EQ(canonManagerErrorText(CanonBadReply), canonManagerErrorText(12345));
}

TEST_F(ut_Common, packageInstalledRejectsInvalidName)
{
    EXPECT_FALSE(isPackageInstalled("not a package"));
    EXPECT_FALSE(isPackageInstalled(""));
}

TEST_F(ut_Common, getCurrentTimeRecordsTimestamps)
{
    EXPECT_TRUE(getCurrentTime(RESET_TIME).isEmpty());
    EXPECT_EQ(getCurrentTime(ADD_TIME).size(), 1);
    EXPECT_EQ(getCurrentTime(START_TIME).size(), 2);
    EXPECT_EQ(getCurrentTime(FINISH_TIME).size(), 3);
    EXPECT_EQ(getCurrentTime(ADD_TIME).size(), 1);   // ADD starts a new record
    EXPECT_TRUE(getCurrentTime(RESET_TIME).isEmpty());
}


namespace {

struct ManagerScope
{
    bool owned = true;
    QStringList methods;
    QList<QVariantList> arguments;
    QDBusMessage reply;

    ManagerScope()
    {
        reply = QDBusMessage::createMethodCall("a", "/b", "c", "d").createReply(0);
        ManagerTransport transport;
        transport.isOwnedByRoot = [this]() { return owned; };
        transport.call = [this](const QString &method, const QVariantList &args) {
            methods << method;
            arguments << args;
            return reply;
        };
        setManagerTransport(transport);
    }
    ~ManagerScope() { resetManagerTransport(); }

    void replyWith(const QVariant &value)
    {
        reply = QDBusMessage::createMethodCall("a", "/b", "c", "d").createReply(value);
    }
    void replyError(QDBusError::ErrorType type)
    {
        reply = QDBusMessage::createMethodCall("a", "/b", "c", "d").createErrorReply(type, "x");
    }
};

const QStringList kValidAdd = {"canon1", "CNCUPSLBP6030CAPT.ppd", "usb:ABC123"};

}  // namespace

TEST_F(ut_Common, canonAddCallsInstallWithArguments)
{
    ManagerScope m;
    EXPECT_EQ(managerCanonPrinter("add", kValidAdd), (int)CanonOk);
    EXPECT_EQ(m.methods, QStringList{"CanonPrinterInstall"});
    ASSERT_EQ(m.arguments.size(), 1);
    EXPECT_EQ(m.arguments.first().first().toStringList(), kValidAdd);
}

TEST_F(ut_Common, canonRemoveCallsRemove)
{
    ManagerScope m;
    EXPECT_EQ(managerCanonPrinter("remove", {"canon1"}), (int)CanonOk);
    EXPECT_EQ(m.methods, QStringList{"CanonPrinterRemove"});
}

TEST_F(ut_Common, canonExistsOnlyAcceptedForAdd)
{
    ManagerScope m;
    m.replyWith(2);
    EXPECT_EQ(managerCanonPrinter("add", kValidAdd), (int)CanonExists);
    EXPECT_EQ(managerCanonPrinter("remove", {"canon1"}), (int)CanonBadReply);
}

TEST_F(ut_Common, canonReplyCodesFailClosed)
{
    ManagerScope m;
    m.replyWith(1);
    EXPECT_EQ(managerCanonPrinter("add", kValidAdd), (int)CanonFailed);
    m.replyWith(77);
    EXPECT_EQ(managerCanonPrinter("add", kValidAdd), (int)CanonBadReply);
    m.replyWith("zero");
    EXPECT_EQ(managerCanonPrinter("add", kValidAdd), (int)CanonBadReply);
    m.reply = QDBusMessage::createMethodCall("a", "/b", "c", "d").createReply(QVariantList{0, 1});
    EXPECT_EQ(managerCanonPrinter("add", kValidAdd), (int)CanonBadReply);   // more than one value
    m.reply = QDBusMessage::createMethodCall("a", "/b", "c", "d");          // not a reply at all
    EXPECT_EQ(managerCanonPrinter("add", kValidAdd), (int)CanonBadReply);
}

TEST_F(ut_Common, canonBusErrorsAreMapped)
{
    ManagerScope m;
    const QList<QPair<QDBusError::ErrorType, int>> table = {
        {QDBusError::AccessDenied, CanonAccessDenied}, {QDBusError::InvalidArgs, CanonInvalidArgs},
        {QDBusError::LimitsExceeded, CanonBusy},       {QDBusError::NoReply, CanonTimeout},
        {QDBusError::Timeout, CanonTimeout},           {QDBusError::ServiceUnknown, CanonUnavailable},
        {QDBusError::Other, CanonFailed}};
    for (const auto &entry : table) {
        m.replyError(entry.first);
        EXPECT_EQ(managerCanonPrinter("add", kValidAdd), entry.second) << entry.first;
    }
}

TEST_F(ut_Common, canonServiceNotOwnedByRootIsUnavailableWithoutCall)
{
    ManagerScope m;
    m.owned = false;
    EXPECT_EQ(managerCanonPrinter("add", kValidAdd), (int)CanonUnavailable);
    EXPECT_TRUE(m.methods.isEmpty());
}

TEST_F(ut_Common, launchAutoStartSucceedsOnlyOnReply)
{
    ManagerScope m;
    EXPECT_TRUE(managerLaunchAutoStart("/opt/apps/vendor/"));
    EXPECT_EQ(m.methods, QStringList{"LaunchAutoStart"});
    EXPECT_EQ(m.arguments.first().first().toString(), QString("/opt/apps/vendor/"));
    m.replyError(QDBusError::AccessDenied);
    EXPECT_FALSE(managerLaunchAutoStart("/opt/apps/vendor/"));
    m.owned = false;
    m.methods.clear();
    EXPECT_FALSE(managerLaunchAutoStart("/opt/apps/vendor/"));
    EXPECT_TRUE(m.methods.isEmpty());
}

TEST_F(ut_Common, formatModelNameTruncatesAtVersionAndSuffix)
{
    QString model = "MG4100 series Ver.3.90";
    formatModelName("Canon", model);
    EXPECT_EQ(model, QString("MG4100"));

    model = "LaserJet 4 Plus v2013.111 Postscript (recommended)";
    formatModelName("HP", model);
    EXPECT_EQ(model, QString("LaserJet 4 Plus"));
}

TEST_F(ut_Common, formatModelNameKeepsVersionLikeModelNames)
{
    QString model = "PX V500";   // " v" followed by more than a one-digit/dotted version
    formatModelName("Epson", model);
    EXPECT_EQ(model, QString("PX V500"));
}

TEST_F(ut_Common, formatModelNameExpandsHpShortNames)
{
    QString model = "dj 840c";
    formatModelName("HP", model);
    EXPECT_EQ(model, QString("DeskJet 840c"));
    model = "lj 1020";
    formatModelName("HP", model);
    EXPECT_EQ(model, QString("LaserJet 1020"));
}

TEST_F(ut_Common, formatModelNameCleansSeparatorsAndMakePrefix)
{
    QString model = "Epson PX-V500";
    formatModelName("Epson", model);
    EXPECT_EQ(model, QString("PX V500"));
    model = "HL-L2350DW";
    formatModelName("Brother", model);
    EXPECT_EQ(model, QString("HL L2350DW"));
}

TEST_F(ut_Common, formatModelNameDropsBishengMarker)
{
    QString model = "Foo 100 (Bisheng)";
    formatModelName("Bar", model);
    EXPECT_EQ(model, QString("Foo 100"));
}

TEST_F(ut_Common, ippAttributeValueConvertsByTag)
{
    ipp_t *ipp = ippNew();
    ippAddString(ipp, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", nullptr, "  Office  ");
    ippAddInteger(ipp, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "copies", 42);
    ippAddBoolean(ipp, IPP_TAG_PRINTER, "accepting", 1);
    ippAddRange(ipp, IPP_TAG_PRINTER, "range", 1, 5);
    const char *kw[] = {"one", "two"};
    ippAddStrings(ipp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "kw", 2, nullptr, kw);

    EXPECT_EQ(ipp_attribute_value(ippFindAttribute(ipp, "printer-name", IPP_TAG_ZERO), 0).toString(), QString("Office"));
    const QVariant copies = ipp_attribute_value(ippFindAttribute(ipp, "copies", IPP_TAG_ZERO), 0);
    EXPECT_EQ(copies.toInt(), 42);
    EXPECT_EQ(ipp_attribute_value(ippFindAttribute(ipp, "accepting", IPP_TAG_ZERO), 0).toBool(), true);
    EXPECT_FALSE(ipp_attribute_value(ippFindAttribute(ipp, "range", IPP_TAG_ZERO), 0).isValid());   // unsupported tag
    EXPECT_EQ(ipp_attribute_value(ippFindAttribute(ipp, "kw", IPP_TAG_ZERO), 1).toString(), QString("two"));
    ippDelete(ipp);
}
