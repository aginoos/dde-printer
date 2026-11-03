// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

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

namespace {
// Replaces the CUPS calls of DefaultAddPrinter with in-memory state.
class FakeCupsAddPrinter : public DefaultAddPrinter
{
public:
    FakeCupsAddPrinter(const TDeviceInfo &printer, const QStringList &before, const QStringList &after)
        : DefaultAddPrinter(printer, solutionWith("generic.ppd"), QString()), m_before(before), m_after(after)
    {
    }
    void simulateFailedRun()
    {
        m_calls = 0;
        rememberExistingQueue();
        rollbackFailedQueue();
    }
    QStringList removed;
    bool removeResult = true;

protected:
    QStringList existingQueueNames() override { return m_calls++ == 0 ? m_before : m_after; }
    bool removeQueue(const QString &name) override
    {
        removed << name;
        return removeResult;
    }

private:
    QStringList m_before;
    QStringList m_after;
    int m_calls = 0;
};
}   // namespace

TEST(ut_AddPrinter, rollbackOnlyForQueuesCreatedByTheTask)
{
    EXPECT_TRUE(shouldRollbackQueue(false, true));
    EXPECT_FALSE(shouldRollbackQueue(true, true));
    EXPECT_FALSE(shouldRollbackQueue(false, false));
    EXPECT_FALSE(shouldRollbackQueue(true, false));
}

TEST(ut_AddPrinter, failedAddRemovesTheLeftoverQueue)
{
    TDeviceInfo printer = deviceWith({"ipp://h/printers/x"});
    printer.strName = "vtest-1";
    FakeCupsAddPrinter task(printer, {"Other"}, {"Other", "vtest-1"});
    task.simulateFailedRun();
    EXPECT_EQ(task.removed, QStringList{"vtest-1"});
}

TEST(ut_AddPrinter, failedAddKeepsAPreExistingQueue)
{
    TDeviceInfo printer = deviceWith({"ipp://h/printers/x"});
    printer.strName = "vtest-1";
    FakeCupsAddPrinter task(printer, {"vtest-1"}, {"vtest-1"});
    task.simulateFailedRun();
    EXPECT_TRUE(task.removed.isEmpty());
}

TEST(ut_AddPrinter, failedRollbackIsOnlyLogged)
{
    TDeviceInfo printer = deviceWith({"ipp://h/printers/x"});
    printer.strName = "vtest-1";
    FakeCupsAddPrinter task(printer, {}, {"vtest-1"});
    task.removeResult = false;
    EXPECT_NO_THROW(task.simulateFailedRun());
    EXPECT_EQ(task.removed.size(), 1);
}


// ============================================================ full task flows

namespace {

template<typename Condition>
bool pumpUntil(Condition done, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

// Puts every replaceable piece of addprinter.cpp back to its real default.
struct SeamGuard
{
    ~SeamGuard()
    {
        g_captexec = "/opt/cndrvcups-capt/add";
        g_vendorRoot = "/opt/printer-drivers";
        g_lpadmin = "/usr/sbin/lpadmin";
        g_canonManager = nullptr;
        g_vendorAutostart = nullptr;
        g_driverDepends = nullptr;
        g_systemPrinterNames = nullptr;
        setCanonPrerequisite(nullptr);
        InstallBackend::reset();
    }
};

// A stand-in for lpadmin: records the argument list, prints `err` on stderr, exits with `code`.
struct FakeLpadmin
{
    FakeLpadmin(int code, const QString &err)
    {
        QFile file(path());
        file.open(QIODevice::WriteOnly);
        file.write(("#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + dir.filePath("args") + "'\n"
                    "printf '" + err + "' >&2\nexit " + QString::number(code) + "\n").toUtf8());
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    QString path() const { return dir.filePath("lpadmin"); }
    QStringList args() const
    {
        QFile file(dir.filePath("args"));
        if (!file.open(QIODevice::ReadOnly))
            return QStringList();
        return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    }
    QTemporaryDir dir;
};

// No vendor directories, no extra driver dependencies, no existing queues.
void quietSystem(const QTemporaryDir &vendorRoot)
{
    g_vendorRoot = vendorRoot.path();
    g_driverDepends = [](const QString &) { return QStringList(); };
    g_systemPrinterNames = []() { return QStringList(); };
}

TDeviceInfo officePrinter()
{
    TDeviceInfo printer = deviceWith({"usb://Acme/X1?serial=AB12"});
    printer.strName = "Office Laser";
    printer.serial = "AB12";
    return printer;
}

QMap<QString, QVariant> acmeSolution(const QString &ppd = "generic.ppd", int from = PPDFrom_Database)
{
    QMap<QString, QVariant> solution = solutionWith(ppd, from);
    solution[CUPS_PPD_MAKE_MODEL] = "Acme X1";
    return solution;
}

// DefaultAddPrinter with the CUPS queue list and deletion replaced by memory.
class MemoryCupsPrinter : public DefaultAddPrinter
{
public:
    using DefaultAddPrinter::DefaultAddPrinter;
    QStringList before;
    QStringList after;
    QStringList removed;

protected:
    QStringList existingQueueNames() override { return calls++ == 0 ? before : after; }
    bool removeQueue(const QString &name) override
    {
        removed << name;
        return true;
    }

private:
    int calls = 0;
};

struct StatusCapture
{
    explicit StatusCapture(AddPrinterTask &task)
    {
        QObject::connect(&task, &AddPrinterTask::signalStatus, &task, [this](int s) { seen.append(s); });
    }
    bool done() const { return !seen.isEmpty(); }
    QList<int> seen;
};

}  // namespace

TEST(ut_AddPrinterFlow, fillPrinterInfoDerivesLocationAndDescription)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);

    StubTask usb(officePrinter(), acmeSolution());
    EXPECT_EQ(usb.fillPrinterInfo(), 0);
    EXPECT_EQ(usb.m_printer.strName, QString("Office-Laser"));
    EXPECT_EQ(usb.m_printer.strLocation, QString("Direct-attached Device"));
    EXPECT_EQ(usb.m_printer.strInfo, QString("Acme X1"));

    TDeviceInfo network = deviceWith({"socket://printer.example:9100"}, "network");
    network.strName = "Net";
    QMap<QString, QVariant> withModel = acmeSolution();
    withModel[CUPS_PPD_MODEL] = "X1 Model";
    StubTask net(network, withModel);
    net.fillPrinterInfo();
    EXPECT_EQ(net.m_printer.strLocation, QString("printer.example"));
    EXPECT_EQ(net.m_printer.strInfo, QString("X1 Model"));   // the short model wins over make-and-model

    TDeviceInfo file = deviceWith({"file:///tmp/out.pdf"});
    file.strName = "Pdf";
    file.strLocation = "kept";
    file.strInfo = "kept too";
    StubTask pdf(file, acmeSolution());
    pdf.fillPrinterInfo();
    EXPECT_EQ(pdf.m_printer.strLocation, QString("kept"));
    EXPECT_EQ(pdf.m_printer.strInfo, QString("kept too"));

    TDeviceInfo plainFile = deviceWith({"file:///tmp/out.pdf"});
    plainFile.strName = "Pdf";
    StubTask plain(plainFile, acmeSolution());
    plain.fillPrinterInfo();
    EXPECT_EQ(plain.m_printer.strLocation, QString("File"));
}

TEST(ut_AddPrinterFlow, queueNamesAreMadeUniqueIgnoringCase)
{
    SeamGuard guard;
    g_systemPrinterNames = []() { return QStringList{"office-laser", "OFFICE-LASER-1", "Other"}; };
    TDeviceInfo printer = officePrinter();
    EXPECT_EQ(AddPrinterFactory::getInstance()->defaultPrinterName(printer, acmeSolution()), QString("Office-Laser-2"));

    g_systemPrinterNames = []() { return QStringList{"Other"}; };
    EXPECT_EQ(AddPrinterFactory::getInstance()->defaultPrinterName(printer, acmeSolution()), QString("Office-Laser"));
}

TEST(ut_AddPrinterFlow, databaseDriverIsAddedWithLpadminAndKeepsTheQueuePrivate)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    FakeLpadmin lpadmin(0, "");
    g_lpadmin = lpadmin.path();

    MemoryCupsPrinter task(officePrinter(), acmeSolution(), "usb://Acme/X1?serial=AB12");
    StatusCapture outcome(task);
    EXPECT_EQ(task.doWork(), 0);
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Suc});
    EXPECT_TRUE(task.removed.isEmpty());
    EXPECT_EQ(lpadmin.args(), (QStringList{"-p", "Office-Laser", "-E", "-v", "usb://Acme/X1?serial=AB12", "-m", "generic.ppd",
                                           "-D", "Acme X1", "-L", "Direct-attached Device", "-o", "printer-is-shared=false"}));
}

TEST(ut_AddPrinterFlow, ppdFileAndEverywhereDriversUseTheirOwnOptions)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    QTemporaryDir ppdDir;
    const QString ppd = ppdDir.filePath("custom.ppd");
    QFile(ppd).open(QIODevice::WriteOnly);

    {
        FakeLpadmin lpadmin(0, "");
        g_lpadmin = lpadmin.path();
        MemoryCupsPrinter task(officePrinter(), acmeSolution(ppd, PPDFrom_File), "u");
        StatusCapture outcome(task);
        task.doWork();
        ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
        EXPECT_EQ(outcome.seen, QList<int>{TStat_Suc});
        const QStringList args = lpadmin.args();
        EXPECT_EQ(args.at(args.indexOf("-P") + 1), ppd);
        EXPECT_FALSE(args.contains("-m"));
    }
    {
        FakeLpadmin lpadmin(0, "");
        g_lpadmin = lpadmin.path();
        MemoryCupsPrinter task(officePrinter(), acmeSolution("everywhere", PPDFrom_EveryWhere), "u");
        StatusCapture outcome(task);
        task.doWork();
        ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
        EXPECT_EQ(outcome.seen, QList<int>{TStat_Suc});
        const QStringList args = lpadmin.args();
        EXPECT_EQ(args.at(args.indexOf("-m") + 1), QString("everywhere"));
    }
}

TEST(ut_AddPrinterFlow, failedLpadminReportsTheRealErrorAndRemovesTheNewQueue)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    FakeLpadmin lpadmin(1, "lpadmin: first line is dropped\\nlpadmin: the device is not reachable\\n");
    g_lpadmin = lpadmin.path();

    MemoryCupsPrinter task(officePrinter(), acmeSolution(), "u");
    task.before = {"Other"};
    task.after = {"Other", "Office-Laser"};
    StatusCapture outcome(task);
    task.doWork();
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(task.getErrorMassge().contains("the device is not reachable"));
    EXPECT_FALSE(task.getErrorMassge().contains("first line"));
    EXPECT_EQ(task.removed, QStringList{"Office-Laser"});
}

TEST(ut_AddPrinterFlow, failedLpadminKeepsAQueueThatAlreadyExisted)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    FakeLpadmin lpadmin(1, "x\\ny\\n");
    g_lpadmin = lpadmin.path();

    MemoryCupsPrinter task(officePrinter(), acmeSolution(), "u");
    task.before = {"Office-Laser"};
    task.after = {"Office-Laser"};
    StatusCapture outcome(task);
    task.doWork();
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(task.removed.isEmpty());
}

TEST(ut_AddPrinterFlow, stoppedTaskDoesNotStartLpadmin)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    FakeLpadmin lpadmin(0, "");
    g_lpadmin = lpadmin.path();
    MemoryCupsPrinter task(officePrinter(), acmeSolution(), "u");
    task.fillPrinterInfo();
    task.stop();
    EXPECT_EQ(task.addPrinter(), -1);
    EXPECT_TRUE(lpadmin.args().isEmpty());
}

TEST(ut_AddPrinterFlow, missingPpdFileStopsTheFlowBeforeAnyWork)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    FakeLpadmin lpadmin(0, "");
    g_lpadmin = lpadmin.path();
    MemoryCupsPrinter task(officePrinter(), acmeSolution("/nonexistent/ut/a.ppd", PPDFrom_File), "u");
    StatusCapture outcome(task);
    task.doWork();
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(task.getErrorMassge().contains("/nonexistent/ut/a.ppd"));
    EXPECT_TRUE(lpadmin.args().isEmpty());
}

// ---------------------------------------------------------- driver dependencies

TEST(ut_AddPrinterFlow, vendorConfigDirIsFoundByPpdNameAndBadDirectoryNamesAreSkipped)
{
    SeamGuard guard;
    QTemporaryDir root;
    g_vendorRoot = root.path();
    auto writePpds = [&root](const QString &vendor, const QString &content) {
        QDir().mkpath(root.filePath(vendor + "/config"));
        QFile file(root.filePath(vendor + "/config/ppds"));
        file.open(QIODevice::WriteOnly);
        file.write(content.toUtf8());
    };
    writePpds("bad dir", "wanted.ppd\n");      // not a well-formed vendor directory name
    writePpds("vendorA", "other.ppd\n");
    writePpds("vendorB", "first.ppd\nwanted.ppd\n");
    QDir().mkpath(root.filePath("vendorC/config"));   // no ppds file at all

    EXPECT_EQ(findVendorConfigDir("wanted.ppd"), root.path() + "/vendorB/config/");
    EXPECT_EQ(findVendorConfigDir("other.ppd"), root.path() + "/vendorA/config/");
    EXPECT_TRUE(findVendorConfigDir("absent.ppd").isEmpty());
}

TEST(ut_AddPrinterFlow, vendorDriverIsStartedThroughTheServiceOnlyWhenAttended)
{
    SeamGuard guard;
    QTemporaryDir root;
    QDir().mkpath(root.filePath("vendorA/config"));
    QFile ppds(root.filePath("vendorA/config/ppds"));
    ppds.open(QIODevice::WriteOnly);
    ppds.write("generic.ppd\n");
    ppds.close();
    quietSystem(root);
    QStringList started;
    g_vendorAutostart = [&started](const QString &dir) {
        started << dir;
        return true;
    };

    StubTask attended(officePrinter(), acmeSolution());
    EXPECT_EQ(attended.fixDriverDepends(), 0);
    EXPECT_EQ(started, QStringList{root.path() + "/vendorA/config/"});

    started.clear();
    StubTask unattended(officePrinter(), acmeSolution());
    unattended.setUnattended(true);
    EXPECT_EQ(unattended.fixDriverDepends(), -1);
    EXPECT_FALSE(unattended.getErrorMassge().isEmpty());
    EXPECT_TRUE(started.isEmpty());

    StubTask everywhere(officePrinter(), acmeSolution("generic.ppd", PPDFrom_EveryWhere));
    EXPECT_EQ(everywhere.fixDriverDepends(), 0);   // driverless needs no vendor setup
    EXPECT_TRUE(started.isEmpty());
}

TEST(ut_AddPrinterFlow, failingVendorAutostartDoesNotStopTheAdd)
{
    SeamGuard guard;
    QTemporaryDir root;
    QDir().mkpath(root.filePath("vendorA/config"));
    QFile ppds(root.filePath("vendorA/config/ppds"));
    ppds.open(QIODevice::WriteOnly);
    ppds.write("generic.ppd\n");
    ppds.close();
    quietSystem(root);
    g_vendorAutostart = [](const QString &) { return false; };
    StubTask task(officePrinter(), acmeSolution());
    EXPECT_EQ(task.fixDriverDepends(), 0);
}

TEST(ut_AddPrinterFlow, installedDependenciesLetTheFlowContinue)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    QStringList asked;
    g_driverDepends = [&asked](const QString &ppd) {
        asked << ppd;
        return QStringList{"printer-driver-ut-dep-zz"};
    };
    InstallBackend::Hooks hooks;
    hooks.installedVersion = [](const QString &) { return QString("1.0"); };   // already there
    hooks.lastoreService = []() { return QString(); };
    InstallBackend::set(hooks);

    FakeLpadmin lpadmin(0, "");
    g_lpadmin = lpadmin.path();
    MemoryCupsPrinter task(officePrinter(), acmeSolution(), "u");
    StatusCapture outcome(task);
    task.doWork();
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Suc});
    EXPECT_EQ(asked, QStringList{"generic.ppd"});
}

TEST(ut_AddPrinterFlow, refusedDependencyFailsTheAddWithTheInstallerMessage)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    g_driverDepends = [](const QString &) { return QStringList{"openssh-server"}; };   // not a driver package
    FakeLpadmin lpadmin(0, "");
    g_lpadmin = lpadmin.path();

    MemoryCupsPrinter task(officePrinter(), acmeSolution(), "u");
    StatusCapture outcome(task);
    task.doWork();
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(task.getErrorMassge().contains("openssh-server"));
    EXPECT_TRUE(lpadmin.args().isEmpty());   // lpadmin never ran
}

TEST(ut_AddPrinterFlow, stoppedTaskIgnoresInstallerSignals)
{
    SeamGuard guard;
    StubTask task(officePrinter(), acmeSolution());
    StatusCapture outcome(task);
    task.stop();
    task.slotDependsStatus(TStat_Fail);
    task.slotInstallStatus(TStat_Fail);
    task.slotFixHplipStatus(TStat_Fail);
    EXPECT_TRUE(outcome.seen.isEmpty());
}

// ------------------------------------------------------------ server driver

namespace {
QMap<QString, QVariant> solutionWithId(int sid)
{
    QMap<QString, QVariant> solution;
    solution[SD_KEY_sid] = sid;
    return solution;
}
}  // namespace

TEST(ut_AddPrinterFlow, serverAnswerWithAnHttpErrorFailsWithTheSolutionId)
{
    InstallDriver driver(solutionWithId(42));
    QList<int> seen;
    QObject::connect(&driver, &InstallDriver::signalStatus, &driver, [&seen](int s) { seen.append(s); });
    driver.slotServerDone(QNetworkReply::TimeoutError, QByteArray());
    EXPECT_EQ(seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(driver.getErrorString().contains("42"));
    EXPECT_TRUE(driver.getErrorString().contains(QString::number(static_cast<int>(QNetworkReply::TimeoutError))));
}

TEST(ut_AddPrinterFlow, serverAnswerThatIsNotJsonFails)
{
    InstallDriver driver(solutionWithId(1));
    QList<int> seen;
    QObject::connect(&driver, &InstallDriver::signalStatus, &driver, [&seen](int s) { seen.append(s); });
    driver.slotServerDone(QNetworkReply::NoError, "not json {");
    EXPECT_EQ(seen, QList<int>{TStat_Fail});
    EXPECT_FALSE(driver.getErrorString().isEmpty());
}

TEST(ut_AddPrinterFlow, stoppedDriverIgnoresTheServerAnswer)
{
    InstallDriver driver(QMap<QString, QVariant>{});
    QList<int> seen;
    QObject::connect(&driver, &InstallDriver::signalStatus, &driver, [&seen](int s) { seen.append(s); });
    driver.stop();
    driver.slotServerDone(QNetworkReply::TimeoutError, QByteArray());
    EXPECT_TRUE(seen.isEmpty());
}

TEST(ut_AddPrinterFlow, serverAnswerListsThePackagesAndVersionsToInstall)
{
    SeamGuard guard;
    InstallBackend::Hooks hooks;
    hooks.installedVersion = [](const QString &) { return QString(); };
    hooks.lastoreService = []() { return QString(); };
    hooks.isInstallable = [](const QString &, const QString &) { return true; };
    hooks.pkexecPath = []() { return QString(); };   // the apt fallback then stops at once
    InstallBackend::set(hooks);

    InstallDriver driver(solutionWithId(1));
    QList<int> seen;
    QObject::connect(&driver, &InstallDriver::signalStatus, &driver, [&seen](int s) { seen.append(s); });
    // the second version is missing on purpose: it must read as "any version"
    QJsonObject obj;
    obj[SD_KEY_package] = QJsonArray{"printer-driver-ut-a-zz", "printer-driver-ut-b-zz"};
    obj[SD_KEY_ver] = QJsonArray{"1.0"};
    driver.slotServerDone(QNetworkReply::NoError, QJsonDocument(obj).toJson());

    ASSERT_EQ(driver.m_packages.size(), 2);
    EXPECT_EQ(driver.m_packages[0].packageName, QString("printer-driver-ut-a-zz"));
    EXPECT_EQ(driver.m_packages[0].packageVer, QString("1.0"));
    EXPECT_EQ(driver.m_packages[1].packageName, QString("printer-driver-ut-b-zz"));
    EXPECT_TRUE(driver.m_packages[1].packageVer.isEmpty());
    EXPECT_EQ(driver.m_installPackages.size(), 2);
    EXPECT_EQ(seen, QList<int>{TStat_Fail});   // no pkexec: reached the installer and stopped there
}

TEST(ut_AddPrinterFlow, installDriverDoWorkTakesThePackageFromTheSolution)
{
    SeamGuard guard;
    InstallBackend::Hooks hooks;
    hooks.installedVersion = [](const QString &) { return QString("2.0"); };
    hooks.compareVersions = [](const QString &, const QString &) { return PackageUtil::VersionOrder::Equal; };
    hooks.lastoreService = []() { return QString(); };
    InstallBackend::set(hooks);

    QMap<QString, QVariant> solution;
    solution[SD_KEY_driver] = "printer-driver-ut-a-zz";
    solution[SD_KEY_debver] = "2.0";
    InstallDriver driver(solution);
    QList<int> seen;
    QObject::connect(&driver, &InstallDriver::signalStatus, &driver, [&seen](int s) { seen.append(s); });
    driver.doWork();
    ASSERT_EQ(driver.m_packages.size(), 1);
    EXPECT_EQ(driver.m_packages[0].packageVer, QString("2.0"));
    EXPECT_EQ(seen, QList<int>{TStat_Suc});   // already installed at that version
}

TEST(ut_AddPrinterFlow, serverDriversRunTheInstallStepBeforeAdding)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    InstallBackend::Hooks hooks;
    hooks.installedVersion = [](const QString &) { return QString("2.0"); };
    hooks.compareVersions = [](const QString &, const QString &) { return PackageUtil::VersionOrder::Equal; };
    hooks.lastoreService = []() { return QString(); };
    InstallBackend::set(hooks);
    FakeLpadmin lpadmin(0, "");
    g_lpadmin = lpadmin.path();

    QMap<QString, QVariant> solution = acmeSolution("server.ppd", PPDFrom_Server);
    solution[SD_KEY_driver] = "printer-driver-ut-a-zz";
    solution[SD_KEY_debver] = "2.0";
    MemoryCupsPrinter task(officePrinter(), solution, "u");
    StatusCapture outcome(task);
    task.doWork();
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Suc});
    ASSERT_NE(task.m_installDriver, nullptr);
    EXPECT_EQ(lpadmin.args().at(lpadmin.args().indexOf("-m") + 1), QString("server.ppd"));
}

TEST(ut_AddPrinterFlow, failedServerInstallFailsTheAdd)
{
    SeamGuard guard;
    QTemporaryDir vendors;
    quietSystem(vendors);
    QMap<QString, QVariant> solution = acmeSolution("server.ppd", PPDFrom_Server);   // no package name at all
    FakeLpadmin lpadmin(0, "");
    g_lpadmin = lpadmin.path();
    MemoryCupsPrinter task(officePrinter(), solution, "u");
    StatusCapture outcome(task);
    task.doWork();
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(lpadmin.args().isEmpty());
}

// -------------------------------------------------------------- Canon CAPT

namespace {

QMap<QString, QVariant> canonSolution()
{
    return acmeSolution("lsb/usr/CNCUPSLBP2900CAPTK.ppd");
}

struct CanonFixture
{
    CanonFixture()
    {
        QFile(marker.filePath("add")).open(QIODevice::WriteOnly);
        g_captexec = marker.filePath("add");
        quietSystem(vendors);
        g_canonManager = [this](const QString &action, const QStringList &args) {
            actions << action;
            managerArgs = args;
            return managerCode;
        };
    }
    QTemporaryDir marker;
    QTemporaryDir vendors;
    QStringList actions;
    QStringList managerArgs;
    int managerCode = CanonOk;
};

}  // namespace

TEST(ut_AddPrinterFlow, canonUsbPrinterIsAddedThroughTheManagerService)
{
    SeamGuard guard;
    CanonFixture fx;
    AddCanonCAPTPrinter task(officePrinter(), canonSolution(), "AB12");
    StatusCapture outcome(task);
    task.doWork();
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Suc});
    EXPECT_EQ(fx.actions, QStringList{"add"});
    EXPECT_EQ(fx.managerArgs, (QStringList{"Office-Laser", "CNCUPSLBP2900CAPTK.ppd", "usb:AB12"}));
}

TEST(ut_AddPrinterFlow, canonNetworkPrinterIsAddedByItsAddress)
{
    SeamGuard guard;
    CanonFixture fx;
    TDeviceInfo printer = deviceWith({"socket://192.168.1.20:9100"}, "network");
    printer.strName = "Net Canon";
    AddCanonCAPTPrinter task(printer, canonSolution(), "socket://192.168.1.20:9100");
    StatusCapture outcome(task);
    task.doWork();
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Suc});
    EXPECT_EQ(fx.managerArgs.last(), QString("net:192.168.1.20"));
}

TEST(ut_AddPrinterFlow, canonServiceFailureIsReportedWithItsText)
{
    SeamGuard guard;
    CanonFixture fx;
    fx.managerCode = CanonAccessDenied;
    AddCanonCAPTPrinter task(officePrinter(), canonSolution(), "AB12");
    StatusCapture outcome(task);
    task.doWork();
    ASSERT_TRUE(pumpUntil([&] { return outcome.done(); }));
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(task.getErrorMassge().contains(canonManagerErrorText(CanonAccessDenied)));
}

TEST(ut_AddPrinterFlow, canonUnsafeSerialNeverReachesTheService)
{
    SeamGuard guard;
    CanonFixture fx;
    TDeviceInfo printer = officePrinter();
    printer.serial = "x; reboot";
    AddCanonCAPTPrinter task(printer, canonSolution(), "x; reboot");
    StatusCapture outcome(task);
    task.doWork();
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(fx.actions.isEmpty());
    EXPECT_EQ(task.getErrorMassge(), canonManagerErrorText(CanonInvalidArgs));
}

TEST(ut_AddPrinterFlow, canonWithoutTheVendorToolFails)
{
    SeamGuard guard;
    CanonFixture fx;
    g_captexec = "/nonexistent/ut/capt/add";
    AddCanonCAPTPrinter task(officePrinter(), canonSolution(), "AB12");
    StatusCapture outcome(task);
    task.doWork();
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(fx.actions.isEmpty());
}

TEST(ut_AddPrinterFlow, canonUnattendedNeverUsesPrivilegedSteps)
{
    SeamGuard guard;
    CanonFixture fx;
    bool checked = false;
    setCanonPrerequisite([&checked](const QString &, QString *) {
        checked = true;
        return true;
    });
    AddCanonCAPTPrinter task(officePrinter(), canonSolution(), "AB12");
    task.setUnattended(true);
    StatusCapture outcome(task);
    task.doWork();
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_FALSE(checked);   // the i386 question is for an attended user only
    EXPECT_TRUE(fx.actions.isEmpty());
    EXPECT_FALSE(task.getErrorMassge().isEmpty());
}

TEST(ut_AddPrinterFlow, canonPrerequisiteCanStopTheAdd)
{
    SeamGuard guard;
    CanonFixture fx;
    QString asked;
    setCanonPrerequisite([&asked](const QString &ppd, QString *error) {
        asked = ppd;
        *error = "32-bit support was declined";
        return false;
    });
    AddCanonCAPTPrinter task(officePrinter(), canonSolution(), "AB12");
    StatusCapture outcome(task);
    task.doWork();
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_EQ(asked, QString("CNCUPSLBP2900CAPTK.ppd"));
    EXPECT_EQ(task.getErrorMassge(), QString("32-bit support was declined"));
    EXPECT_TRUE(fx.actions.isEmpty());

    setCanonPrerequisite([](const QString &, QString *) { return true; });
    AddCanonCAPTPrinter allowed(officePrinter(), canonSolution(), "AB12");
    StatusCapture allowedOutcome(allowed);
    allowed.doWork();
    ASSERT_TRUE(pumpUntil([&] { return allowedOutcome.done(); }));
    EXPECT_EQ(allowedOutcome.seen, QList<int>{TStat_Suc});
}

TEST(ut_AddPrinterFlow, stoppedCanonTaskDoesNotCallTheService)
{
    SeamGuard guard;
    CanonFixture fx;
    AddCanonCAPTPrinter task(officePrinter(), canonSolution(), "AB12");
    task.stop();
    EXPECT_EQ(task.addPrinter(), -1);
    EXPECT_TRUE(fx.actions.isEmpty());
}

// ------------------------------------------------------------ hplip backend

TEST(ut_AddPrinterFlow, hplipDriverWithoutHpUriTriesToInstallHplipAndReportsFailure)
{
    SeamGuard guard;
    InstallBackend::Hooks hooks;
    hooks.installedVersion = [](const QString &) { return QString(); };
    hooks.lastoreService = []() { return QString(); };
    hooks.isInstallable = [](const QString &, const QString &) { return true; };
    hooks.pkexecPath = []() { return QString(); };   // no installer on this "system"
    InstallBackend::set(hooks);

    StubTask task(deviceWith({"usb://HP/x?serial=1", "usb://HP/y?serial=2"}), solutionWith("drv:///hpcups.drv/x.ppd"));
    StatusCapture outcome(task);
    EXPECT_EQ(task.isUriAndDriverMatched(), 1);   // asynchronous: waiting for the hplip install
    ASSERT_NE(task.m_fixHplip, nullptr);
    EXPECT_EQ(outcome.seen, QList<int>{TStat_Fail});
    EXPECT_FALSE(task.getErrorMassge().isEmpty());
    EXPECT_FALSE(task.m_fixHplip->getErrorString().isEmpty());
    EXPECT_EQ(task.isUriAndDriverMatched(), -1);   // second attempt: no new installer, plain refusal
}

TEST(ut_AddPrinterFlow, hplipBackendWithoutADeviceTaskMatchesNothing)
{
    FixHplipBackend backend;
    EXPECT_TRUE(backend.getMatchHplipUri("hp:/usb/x?serial=1").isEmpty());
    EXPECT_TRUE(backend.getErrorString().isEmpty());
}

namespace {

TDeviceInfo hpDevice(const QString &uri)
{
    TDeviceInfo device = deviceWith({uri});
    return device;
}

}  // namespace

TEST(ut_AddPrinterFlow, hplipMatchUsesTheSerialOfEachDevice)
{
    FixHplipBackend backend;
    backend.m_deviceTask = new RefreshDevicesByBackendTask();
    backend.m_deviceTask->m_devices = {hpDevice("hp:/usb/X?serial=AAA"), hpDevice("hp:/usb/X?serial=BBB"),
                                       hpDevice("hp:/usb/Y")};

    EXPECT_EQ(backend.getMatchHplipUri("usb://HP/X?serial=bbb"), QString("hp:/usb/X?serial=BBB"));   // case-insensitive
    EXPECT_EQ(backend.getMatchHplipUri("usb://HP/X?serial=AAA&x=1"), QString("hp:/usb/X?serial=AAA"));
    EXPECT_TRUE(backend.getMatchHplipUri("usb://HP/X?serial=CCC").isEmpty());
    EXPECT_TRUE(backend.getMatchHplipUri("usb://HP/X").isEmpty());   // the queried device has no serial

    delete backend.m_deviceTask;
    backend.m_deviceTask = nullptr;
}

TEST(ut_AddPrinterFlow, installedHplipSwitchesTheTaskToTheMatchingHpUri)
{
    StubTask task(deviceWith({"usb://HP/X?serial=BBB", "usb://HP/Y?serial=AAA"}), solutionWith("drv:///hpcups.drv/x.ppd"));
    task.m_fixHplip = new FixHplipBackend(&task);
    task.m_fixHplip->m_deviceTask = new RefreshDevicesByBackendTask();
    task.m_fixHplip->m_deviceTask->m_devices = {hpDevice("hp:/usb/X?serial=AAA"), hpDevice("hp:/usb/X?serial=BBB")};
    task.m_iStep = AddPrinterTask::STEP_Failed;   // nextStep() then only reports; nothing else runs
    StatusCapture capture(task);
    task.slotFixHplipStatus(TStat_Suc);
    EXPECT_EQ(task.m_uri, QString("hp:/usb/X?serial=BBB"));
    EXPECT_EQ(capture.seen, QList<int>{TStat_Fail});

    delete task.m_fixHplip->m_deviceTask;
    task.m_fixHplip->m_deviceTask = nullptr;
}
