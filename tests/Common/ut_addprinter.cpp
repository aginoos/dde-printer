// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>

#include <memory>

#include "ippusbswitch.h"

// Included (and left out of the build list, see Common.pri) to reach the
// file-local helpers and the protected members of AddPrinterTask.
#include "vendor/addprinter.cpp"

namespace {

// AddPrinterTask is abstract; the steps that would touch the system are never run.
class StubTask : public AddPrinterTask
{
public:
    StubTask(const TDeviceInfo &printer, const QMap<QString, QVariant> &solution, const QString &uri = QString())
        : AddPrinterTask(printer, solution, uri)
    {
    }
    int addPrinter() override { return 0; }
    using AddPrinterTask::checkUriAndDriver;
    using AddPrinterTask::isUriAndDriverMatched;
};

TDeviceInfo deviceWith(const QStringList &uris, const QString &cls = "direct")
{
    TDeviceInfo info;
    info.uriList = uris;
    info.strClass = cls;
    return info;
}

QMap<QString, QVariant> solutionWith(const QString &ppd, int from = PPDFrom_Database)
{
    QMap<QString, QVariant> solution;
    solution[CUPS_PPD_NAME] = ppd;
    solution[SD_KEY_from] = from;
    return solution;
}

}  // namespace

// ------------------------------------------------------------- small helpers

TEST(ut_AddPrinter, canonCaptDriverNamesAreRecognised)
{
    EXPECT_TRUE(isCanonCAPTDrv("CNCUPSLBP2900CAPTK.ppd"));
    EXPECT_TRUE(isCanonCAPTDrv("lsb/usr/CNCUPSLBP6030CAPTK.ppd"));
    EXPECT_FALSE(isCanonCAPTDrv("CNCUPSMF230.ppd"));
    EXPECT_FALSE(isCanonCAPTDrv(QString()));
}

TEST(ut_AddPrinter, hplipDriverPrefixesAreRecognised)
{
    EXPECT_TRUE(isHplipDrv("drv:///hpcups.drv/hp-laserjet.ppd"));
    EXPECT_TRUE(isHplipDrv("drv:///hpijs.drv/x.ppd"));
    EXPECT_TRUE(isHplipDrv("lsb/usr/hplip/x.ppd"));
    EXPECT_TRUE(isHplipDrv("hplip:0/ppd/x.ppd"));
    EXPECT_TRUE(isHplipDrv("hp/hp-color.ppd"));
    EXPECT_FALSE(isHplipDrv("drv:///sample.drv/generic.ppd"));
    EXPECT_FALSE(isHplipDrv(QString()));
}

TEST(ut_AddPrinter, driverSourceAndSearchTypeNames)
{
    EXPECT_EQ(getDriverFrom(PPDFrom_File), QString("file"));
    EXPECT_EQ(getDriverFrom(PPDFrom_Server), QString("network"));
    EXPECT_EQ(getDriverFrom(PPDFrom_EveryWhere), QString("everywhere"));
    EXPECT_EQ(getDriverFrom(PPDFrom_Database), QString("local"));
    EXPECT_EQ(getDriverFrom(99), QString("local"));

    EXPECT_EQ(getSearchType(InfoFrom_Detect), QString("auto"));
    EXPECT_EQ(getSearchType(InfoFrom_Host), QString("host"));
    EXPECT_EQ(getSearchType(InfoFrom_Manual), QString("uri"));
    EXPECT_EQ(getSearchType(InfoFrom_Invalid), QString("unknown"));
}

TEST(ut_AddPrinter, ipAddressDetection)
{
    EXPECT_TRUE(containsIpAddress("socket://192.168.1.20:9100"));
    EXPECT_TRUE(containsIpAddress("10.0.0.1"));
    EXPECT_FALSE(containsIpAddress("usb://Canon/LBP2900?serial=ABC"));
    EXPECT_FALSE(containsIpAddress("1.2.3"));
}

TEST(ut_AddPrinter, canonDeviceInfoFromNetworkUri)
{
    QString info;
    EXPECT_TRUE(buildCanonDeviceInfo("socket://192.168.1.20:9100", QString(), info));
    EXPECT_EQ(info, QString("net:192.168.1.20"));
}

TEST(ut_AddPrinter, canonDeviceInfoRejectsImpossibleAddress)
{
    QString info;
    EXPECT_FALSE(buildCanonDeviceInfo("socket://999.999.999.999", "SERIAL1", info));
    EXPECT_TRUE(info.isEmpty());
}

TEST(ut_AddPrinter, canonDeviceInfoFromSerial)
{
    QString info;
    EXPECT_TRUE(buildCanonDeviceInfo("usb://Canon/LBP", "AB12-cd_3", info));
    EXPECT_EQ(info, QString("usb:AB12-cd_3"));
}

TEST(ut_AddPrinter, canonDeviceInfoRejectsUnsafeSerial)
{
    QString info;
    EXPECT_FALSE(buildCanonDeviceInfo("usb://Canon/LBP", "x; reboot", info));
    EXPECT_FALSE(buildCanonDeviceInfo("usb://Canon/LBP", QString(), info));
    EXPECT_FALSE(buildCanonDeviceInfo("usb://Canon/LBP", QString(64 + 1, 'a'), info));
    EXPECT_TRUE(info.isEmpty());
}

// ----------------------------------------------------------- InstallDriver

TEST(ut_AddPrinter, installDriverWithoutPackageNameFails)
{
    InstallDriver driver(QMap<QString, QVariant>{});
    int status = -1;
    QObject::connect(&driver, &InstallDriver::signalStatus, &driver, [&status](int s) { status = s; });
    driver.doWork();
    EXPECT_EQ(status, static_cast<int>(TStat_Fail));
}

// ------------------------------------------------------ URI / driver checks

TEST(ut_AddPrinter, emptyUriListIsRejected)
{
    StubTask task(deviceWith({}), solutionWith("x.ppd"));
    EXPECT_EQ(task.checkUriAndDriver(), -1);
    EXPECT_FALSE(task.getErrorMassge().isEmpty());
}

TEST(ut_AddPrinter, missingPpdFileIsRejected)
{
    StubTask task(deviceWith({"usb://a/b"}), solutionWith("/nonexistent/ut/file.ppd", PPDFrom_File));
    EXPECT_EQ(task.checkUriAndDriver(), -2);
    EXPECT_TRUE(task.getErrorMassge().contains("/nonexistent/ut/file.ppd"));
}

TEST(ut_AddPrinter, existingPpdFileIsAccepted)
{
    StubTask task(deviceWith({"usb://a/b"}), solutionWith("/proc/self/exe", PPDFrom_File));
    EXPECT_EQ(task.checkUriAndDriver(), 0);
}

TEST(ut_AddPrinter, networkQueuesAndSingleUrisSkipTheMatch)
{
    StubTask network(deviceWith({"hp:/usb/x", "usb://x"}, "network"), solutionWith("drv:///hpcups.drv/x.ppd"), "orig");
    EXPECT_EQ(network.isUriAndDriverMatched(), 0);
    EXPECT_EQ(network.m_uri, QString("orig"));

    StubTask single(deviceWith({"usb://x"}), solutionWith("x.ppd"), "orig");
    EXPECT_EQ(single.isUriAndDriverMatched(), 0);
    EXPECT_EQ(single.m_uri, QString("orig"));
}

TEST(ut_AddPrinter, hplipDriverPicksTheHpUri)
{
    StubTask task(deviceWith({"usb://HP/x?serial=1", "hp:/usb/x?serial=1"}), solutionWith("drv:///hpcups.drv/x.ppd"));
    EXPECT_EQ(task.isUriAndDriverMatched(), 0);
    EXPECT_EQ(task.m_uri, QString("hp:/usb/x?serial=1"));
}

TEST(ut_AddPrinter, otherDriverPicksTheUsbUri)
{
    StubTask task(deviceWith({"hp:/usb/x?serial=1", "usb://HP/x?serial=1"}), solutionWith("generic.ppd"));
    EXPECT_EQ(task.isUriAndDriverMatched(), 0);
    EXPECT_EQ(task.m_uri, QString("usb://HP/x?serial=1"));
}

TEST(ut_AddPrinter, otherDriverWithOnlyHpUrisAsksForAnHplipDriver)
{
    StubTask task(deviceWith({"hp:/usb/a", "hp:/usb/b"}), solutionWith("generic.ppd"));
    EXPECT_EQ(task.isUriAndDriverMatched(), -1);
    EXPECT_TRUE(task.m_uri.isEmpty());
    EXPECT_FALSE(task.getErrorMassge().isEmpty());
}

// ------------------------------------------------------------ ipp-usb switch

TEST(ut_AddPrinter, adaptLeavesOrdinaryUrisAlone)
{
    TDeviceInfo printer = deviceWith({"socket://192.168.1.2:9100"});
    QString uri = "keep";
    QString error;
    EXPECT_TRUE(adaptIppUsbUri(printer, solutionWith("x.ppd"), uri, error));
    EXPECT_EQ(uri, QString("keep"));
    EXPECT_EQ(printer.uriList, QStringList{"socket://192.168.1.2:9100"});
    EXPECT_TRUE(error.isEmpty());

    TDeviceInfo none = deviceWith({});
    EXPECT_TRUE(adaptIppUsbUri(none, solutionWith("x.ppd"), uri, error));
}

TEST(ut_AddPrinter, adaptReportsWhenTheDnssdAddressIsMissing)
{
    TDeviceInfo printer = deviceWith({"ipp://localhost:60999/ipp/print"});
    QString uri = "keep";
    QString error;
    const IppUsbSwitch::Result direct = IppUsbSwitch::driverlessUri(printer.uriList.first());
    if (direct.ok)
        GTEST_SKIP() << "an ipp-usb service is really published on this machine";

    EXPECT_FALSE(adaptIppUsbUri(printer, solutionWith("everywhere", PPDFrom_EveryWhere), uri, error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(uri, QString("keep"));                                          // nothing is half-applied
    EXPECT_EQ(printer.uriList, QStringList{"ipp://localhost:60999/ipp/print"});
}

TEST(ut_AddPrinter, doWorkReportsAnUnreachableIppUsbPrinter)
{
    TDeviceInfo printer = deviceWith({"ipp://localhost:60999/ipp/print"});
    if (IppUsbSwitch::driverlessUri(printer.uriList.first()).ok)
        GTEST_SKIP() << "an ipp-usb service is really published on this machine";

    StubTask task(printer, solutionWith("everywhere", PPDFrom_EveryWhere));
    QList<int> seen;
    QObject::connect(&task, &AddPrinterTask::signalStatus, &task, [&seen](int s) { seen.append(s); });
    EXPECT_EQ(task.doWork(), -1);
    EXPECT_EQ(seen, QList<int>{TStat_Fail});   // the caller must never be left waiting
    EXPECT_FALSE(task.getErrorMassge().isEmpty());
}

TEST(ut_AddPrinter, stepMachineEmitsTheFinalStatus)
{
    StubTask done(deviceWith({"usb://a"}), solutionWith("x.ppd"));
    QList<int> seen;
    QObject::connect(&done, &AddPrinterTask::signalStatus, &done, [&seen](int s) { seen.append(s); });
    done.m_iStep = AddPrinterTask::STEP_Finished;
    done.nextStep();
    EXPECT_EQ(seen, QList<int>{TStat_Suc});

    done.m_iStep = AddPrinterTask::STEP_Failed;
    done.nextStep();
    EXPECT_EQ(seen.last(), static_cast<int>(TStat_Fail));
}

TEST(ut_AddPrinter, stoppedTaskDoesNotRunAnyMoreWork)
{
    StubTask task(deviceWith({"usb://a"}), solutionWith("x.ppd"));
    task.stop();
    EXPECT_TRUE(task.m_bQuit);
}

// ---------------------------------------------------------- default queue name

TEST(ut_AddPrinter, defaultNameKeepsWordsAndDashes)
{
    TDeviceInfo printer = deviceWith({"ipp://h/printers/x"});
    printer.strName = "UtZz Office: Laser (2)";
    EXPECT_EQ(AddPrinterFactory::getInstance()->defaultPrinterName(printer, solutionWith("x.ppd")),
              QString("UtZz-Office-Laser-2"));
}

TEST(ut_AddPrinter, defaultNameFallsBackToTheUriName)
{
    TDeviceInfo printer = deviceWith({"ipp://host/printers/Ut_Zz_Queue"});
    EXPECT_EQ(AddPrinterFactory::getInstance()->defaultPrinterName(printer, solutionWith("x.ppd", PPDFrom_EveryWhere)),
              QString("Ut_Zz_Queue"));
}

TEST(ut_AddPrinter, defaultNameWithNothingUsableIsPrinter)
{
    TDeviceInfo printer = deviceWith({"nothing"});
    printer.strName = "!!!";
    EXPECT_EQ(AddPrinterFactory::getInstance()->defaultPrinterName(printer, solutionWith("x.ppd", PPDFrom_EveryWhere)),
              QString("printer"));
}

TEST(ut_AddPrinter, socketQueuesGetTheirLocationAppended)
{
    TDeviceInfo printer = deviceWith({"socket://192.168.1.2:9100"});
    printer.strName = "UtZz Net";
    printer.strLocation = "Room 1/2";
    EXPECT_EQ(AddPrinterFactory::getInstance()->defaultPrinterName(printer, solutionWith("x.ppd")),
              QString("UtZz-Net-Room-1-2"));

    printer.strLocation = "192.168.1.2";   // a bare address tells nothing
    EXPECT_EQ(AddPrinterFactory::getInstance()->defaultPrinterName(printer, solutionWith("x.ppd")),
              QString("UtZz-Net"));
}

TEST(ut_AddPrinter, defaultNameIsCappedAt127Characters)
{
    TDeviceInfo printer = deviceWith({"ipp://h/printers/x"});
    printer.strName = QString(300, 'a');
    EXPECT_EQ(AddPrinterFactory::getInstance()->defaultPrinterName(printer, solutionWith("x.ppd")).length(), 127);
}

TEST(ut_AddPrinter, factoryChoosesTheTaskByDriver)
{
    TDeviceInfo printer = deviceWith({"usb://Canon/LBP"});
    printer.serial = "AB12";
    EXPECT_EQ(AddPrinterFactory::getInstance()->createAddPrinterTask(printer, solutionWith(QString())), nullptr);

    std::unique_ptr<AddPrinterTask> canon(
        AddPrinterFactory::getInstance()->createAddPrinterTask(printer, solutionWith("CNCUPSLBP2900CAPTK.ppd")));
    ASSERT_NE(canon, nullptr);
    EXPECT_NE(dynamic_cast<AddCanonCAPTPrinter *>(canon.get()), nullptr);

    std::unique_ptr<AddPrinterTask> plain(
        AddPrinterFactory::getInstance()->createAddPrinterTask(printer, solutionWith("generic.ppd")));
    ASSERT_NE(plain, nullptr);
    EXPECT_NE(dynamic_cast<DefaultAddPrinter *>(plain.get()), nullptr);
}

TEST(ut_AddPrinter, lpadminArgsKeepNewQueuesPrivate)
{
    TDeviceInfo printer = deviceWith({"ipp://h/printers/x"});
    printer.strName = "Office";
    const QStringList args = buildLpadminArgs(printer, "ipp://h/printers/x", PPDFrom_Database, "x.ppd");
    EXPECT_EQ(args.mid(0, 7), (QStringList{"-p", "Office", "-E", "-v", "ipp://h/printers/x", "-m", "x.ppd"}));
    ASSERT_GE(args.size(), 2);
    EXPECT_EQ(args.mid(args.size() - 2), (QStringList{"-o", "printer-is-shared=false"}));
}

TEST(ut_AddPrinter, lpadminArgsPickTheDriverOption)
{
    TDeviceInfo printer = deviceWith({"ipp://h/printers/x"});
    printer.strName = "Office";
    printer.strInfo = "desc";
    printer.strLocation = "room 3";
    EXPECT_TRUE(buildLpadminArgs(printer, "u", PPDFrom_EveryWhere, "ignored").contains("everywhere"));
    const QStringList file = buildLpadminArgs(printer, "u", PPDFrom_File, "/tmp/a.ppd");
    EXPECT_EQ(file.at(file.indexOf("-P") + 1), QString("/tmp/a.ppd"));
    EXPECT_EQ(file.at(file.indexOf("-D") + 1), QString("desc"));
    EXPECT_EQ(file.at(file.indexOf("-L") + 1), QString("room 3"));
    EXPECT_TRUE(file.contains("printer-is-shared=false"));
}
