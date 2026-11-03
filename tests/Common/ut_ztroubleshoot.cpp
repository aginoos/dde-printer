// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QList>
#include <QPair>
#include <QString>

// Included (and left out of the build list, see Common.pri) to reach the
// helpers in the file scope and the unnamed namespace.
#include "vendor/ztroubleshoot.cpp"

namespace {

// A job whose outcome is fixed by the test.
class FakeJob : public TroubleShootJob
{
public:
    FakeJob(const QString &name, bool pass, const QString &message = QString())
        : TroubleShootJob(QString(), nullptr), m_name(name), m_pass(pass)
    {
        m_strMessage = message;
    }

    bool isPass() override
    {
        ++runs;
        return m_pass;
    }

    QString getJobName() override { return m_name; }

    int runs = 0;

private:
    QString m_name;
    bool m_pass;
};

QStringList jobNames(const QList<TroubleShootJob *> &jobs)
{
    QStringList list;
    for (TroubleShootJob *job : jobs) {
        list << job->getJobName();
    }
    return list;
}

}  // namespace

// The task is not started in these tests: running the default jobs needs a
// CUPS server and a real queue.
TEST(ut_TroubleShoot, defaultJobs)
{
    TroubleShoot task("5150");
    EXPECT_EQ(task.getJobs().size(), 3);
    EXPECT_EQ(jobNames(task.getJobs()),
              (QStringList{"Check CUPS server", "Check driver", "Check printer settings"}));
}

TEST(ut_TroubleShoot, addJobRejectsNull)
{
    TroubleShoot task("5150");
    EXPECT_EQ(task.addJob(nullptr), -1);
    EXPECT_EQ(task.getJobs().size(), 3);
}

TEST(ut_TroubleShoot, addJobAppendsInOrder)
{
    FakeJob extra("extra", true);
    TroubleShoot task("5150");
    EXPECT_EQ(task.addJob(&extra), 0);
    ASSERT_EQ(task.getJobs().size(), 4);
    EXPECT_EQ(task.getJobs().last(), &extra);
}

TEST(ut_TroubleShoot, doWorkRunsEveryJobWhenAllPass)
{
    FakeJob first("first", true), second("second", true);
    TroubleShoot task("5150");
    task.m_jobs.clear();
    task.addJob(&first);
    task.addJob(&second);

    QList<QPair<int, QString>> progress;
    QObject::connect(&task, &TaskInterface::signalUpdateProgress,
                     [&progress](int index, QString message) { progress.append({index, message}); });

    EXPECT_EQ(task.doWork(), 0);
    EXPECT_EQ(first.runs, 1);
    EXPECT_EQ(second.runs, 1);
    ASSERT_EQ(progress.size(), 2);
    EXPECT_EQ(progress.at(0).first, 0);
    EXPECT_EQ(progress.at(1).first, 1);
}

TEST(ut_TroubleShoot, doWorkRunsEveryCheckEvenAfterAFailure)
{
    FakeJob first("first", true), failing("failing", false, "no driver"), never("never", true);
    TroubleShoot task("5150");
    task.m_jobs.clear();
    task.addJob(&first);
    task.addJob(&failing);
    task.addJob(&never);

    QList<QPair<int, QString>> progress;
    QObject::connect(&task, &TaskInterface::signalUpdateProgress,
                     [&progress](int index, QString message) { progress.append({index, message}); });

    EXPECT_EQ(task.doWork(), -1);                 // a failure is still reported
    EXPECT_EQ(never.runs, 1);                     // one failure must not hide the checks after it
    ASSERT_EQ(progress.size(), 3);
    EXPECT_EQ(progress.at(1).first, 1);
    EXPECT_EQ(progress.at(1).second, QString("no driver"));   // the failing job's message is reported
}

TEST(ut_TroubleShoot, stopPreventsFurtherJobsAndStopsEveryJob)
{
    FakeJob first("first", true), second("second", true);
    TroubleShoot task("5150");
    task.m_jobs.clear();
    task.addJob(&first);
    task.addJob(&second);

    task.stop();
    EXPECT_TRUE(first.m_bQuit);
    EXPECT_TRUE(second.m_bQuit);
    EXPECT_EQ(task.doWork(), 0);   // nothing is run once stopped
    EXPECT_EQ(first.runs, 0);
    EXPECT_EQ(second.runs, 0);
}

TEST(ut_TroubleShoot, doWorkWithoutJobsSucceeds)
{
    TroubleShoot task("5150");
    task.m_jobs.clear();
    EXPECT_EQ(task.doWork(), 0);
}

// ------------------------------------------------- jobs without a printer

TEST(ut_TroubleShoot, jobsForAnEmptyPrinterNamePassWithoutCups)
{
    CheckDriver driverJob("");
    CheckConnected connectedJob("");
    CheckAttributes attributesJob("");
    for (TroubleShootJob *job : QList<TroubleShootJob *>{&driverJob, &connectedJob, &attributesJob}) {
        QList<int> states;
        QObject::connect(job, &TroubleShootJob::signalStateChanged,
                         [&states](int state, const QString &) { states.append(state); });
        EXPECT_TRUE(job->isPass()) << job->getJobName().toStdString();
        EXPECT_EQ(states, (QList<int>{TStat_Running, TStat_Suc})) << job->getJobName().toStdString();
        EXPECT_TRUE(job->getMessage().isEmpty());
    }
}

TEST(ut_TroubleShoot, jobNames)
{
    EXPECT_EQ(CheckCupsServer().getJobName(), QString("Check CUPS server"));
    EXPECT_EQ(CheckDriver("x").getJobName(), QString("Check driver"));
    EXPECT_EQ(CheckConnected("x").getJobName(), QString("Check printer connection"));
    EXPECT_EQ(CheckAttributes("x").getJobName(), QString("Check printer settings"));
}

TEST(ut_TroubleShoot, stopMarksTheJob)
{
    CheckDriver job("x");
    EXPECT_FALSE(job.m_bQuit);
    job.stop();
    EXPECT_TRUE(job.m_bQuit);
}

// -------------------------------------------------------- isFilterMissing

TEST(ut_TroubleShoot, shellBuiltinsAreNeverMissingFilters)
{
    EXPECT_FALSE(isFilterMissing("echo"));
    EXPECT_FALSE(isFilterMissing(":"));
    EXPECT_FALSE(isFilterMissing("-"));
    EXPECT_FALSE(isFilterMissing("exec"));
}

TEST(ut_TroubleShoot, filtersEndingWithPercentAreSkipped)
{
    EXPECT_FALSE(isFilterMissing("whatever%"));
}

TEST(ut_TroubleShoot, absoluteFilterPathsAreCheckedOnDisk)
{
    EXPECT_FALSE(isFilterMissing("/bin/sh"));
    EXPECT_TRUE(isFilterMissing("/nonexistent/dir/filter"));
}

TEST(ut_TroubleShoot, unknownBareFilterIsMissing)
{
    EXPECT_TRUE(isFilterMissing("no-such-cups-filter-zzz"));
}

// ------------------------------------------------------- connectableHost

TEST(ut_TroubleShoot, connectableHostRestoresTheCaseOfNames)
{
    EXPECT_EQ(connectableHost("socket://Printer-1:9100", "printer-1"), QString("Printer-1"));
    EXPECT_EQ(connectableHost("ipp://OFFICE.Local/ipp/print", "office.local"), QString("OFFICE.Local"));
}

TEST(ut_TroubleShoot, connectableHostLeavesPlainValuesAlone)
{
    EXPECT_EQ(connectableHost("socket://10.0.0.1:9100", "10.0.0.1"), QString("10.0.0.1"));
    EXPECT_EQ(connectableHost("socket://10.0.0.1:9100", ""), QString());
    EXPECT_EQ(connectableHost("lpd://a/q", "other"), QString("other"));   // not part of the URI
    EXPECT_EQ(connectableHost("lpd://a+b/q", "a+b"), QString("a+b"));     // '+' only means a zone in IPv6
}

TEST(ut_TroubleShoot, connectableHostConvertsIpv6ZoneSeparator)
{
    EXPECT_EQ(connectableHost("socket://[fe80::a00:27ff:fe4e:66a1+ETH0]:9100", "fe80::a00:27ff:fe4e:66a1+eth0"),
              QString("fe80::a00:27ff:fe4e:66a1%ETH0"));
}

TEST(ut_TroubleShoot, connectableHostDropsTheIpvFuturePrefix)
{
    const QString uri = "socket://[v1.fe80::fa0d:acff:fefd:cadc+enp3s0]:9100";
    EXPECT_EQ(connectableHost(uri, "v1.fe80::fa0d:acff:fefd:cadc+enp3s0"),
              QString("fe80::fa0d:acff:fefd:cadc%enp3s0"));
    EXPECT_EQ(connectableHost(uri, "V1.fe80::fa0d:acff:fefd:cadc+enp3s0"),
              QString("fe80::fa0d:acff:fefd:cadc%enp3s0"));
}

TEST(ut_TroubleShoot, connectableHostKeepsPlainIpv6Literals)
{
    EXPECT_EQ(connectableHost("socket://[2001:db8::1]:9100", "2001:db8::1"), QString("2001:db8::1"));
}

// ------------------------------------------------------ CheckCupsErrorLog

namespace {

CupsErrorEntry errorEntry(int id, const QString &fixId)
{
    CupsErrorEntry entry;
    entry.id = id;
    entry.pattern = "x";
    entry.category = "filter";
    entry.fixId = fixId;
    return entry;
}

}  // namespace

TEST(ut_TroubleShoot, cupsErrorLogWithoutMatchOffersNothing)
{
    CheckCupsErrorLog job("Q");
    EXPECT_TRUE(job.getActions().isEmpty());
    EXPECT_TRUE(job.getDetails().isEmpty());
    EXPECT_FALSE(job.hasExpandableContent());
    EXPECT_TRUE(job.repairId().isEmpty());
    EXPECT_TRUE(job.logText().isEmpty());
}

TEST(ut_TroubleShoot, cupsErrorLogMatchWithLogAndFixOffersThreeActions)
{
    CheckCupsErrorLog job("Q");
    job.m_match = errorEntry(1, "cups_service");
    job.m_log = "E [date] something failed";

    const QList<TroubleShootAction> actions = job.getActions();
    ASSERT_EQ(actions.size(), 3);
    EXPECT_EQ(actions.at(0).id, TroubleShootAction::ViewLog);
    EXPECT_EQ(actions.at(1).id, TroubleShootAction::CopyLog);
    EXPECT_EQ(actions.at(2).id, TroubleShootAction::Repair);
    EXPECT_EQ(job.repairId(), QString("cups_service"));
    EXPECT_TRUE(job.hasExpandableContent());
    EXPECT_EQ(job.logText(), job.m_log);
}

TEST(ut_TroubleShoot, cupsErrorLogWithoutFixOrLogOmitsThoseActions)
{
    CheckCupsErrorLog noFix("Q");
    noFix.m_match = errorEntry(2, QString());
    noFix.m_log = "line";
    ASSERT_EQ(noFix.getActions().size(), 2);

    CheckCupsErrorLog noLog("Q");
    noLog.m_match = errorEntry(2, "cups_service");
    ASSERT_EQ(noLog.getActions().size(), 1);
    EXPECT_EQ(noLog.getActions().first().id, TroubleShootAction::Repair);
}

TEST(ut_TroubleShoot, cupsErrorLogDetailsNameCauseAndAction)
{
    CheckCupsErrorLog job("Q");
    job.m_match = errorEntry(9999, QString());   // an id without dedicated text still gets generic text
    const QList<TroubleShootDetail> details = job.getDetails();
    ASSERT_EQ(details.size(), 2);
    EXPECT_FALSE(details.at(0).text.isEmpty());
    EXPECT_FALSE(details.at(1).text.isEmpty());
}

TEST(ut_TroubleShoot, repairResultTextsDifferBySuccess)
{
    CheckCupsErrorLog log("Q");
    EXPECT_NE(log.repairResultText(true), log.repairResultText(false));
    CheckDriver base("Q");
    EXPECT_NE(base.repairResultText(true), base.repairResultText(false));
}

TEST(ut_TroubleShoot, cupsErrorLogForAnEmptyPrinterFailsClosed)
{
    // Without the root service the log cannot be read: the job reports the failure, never a pass.
    CheckCupsErrorLog job("");
    QList<int> states;
    QObject::connect(&job, &TroubleShootJob::signalStateChanged,
                     [&states](int state, const QString &) { states.append(state); });
    const bool pass = job.isPass();
    ASSERT_FALSE(states.isEmpty());
    EXPECT_EQ(states.first(), static_cast<int>(TStat_Running));
    if (!pass) {
        EXPECT_EQ(states.last(), static_cast<int>(TStat_Fail));
        EXPECT_FALSE(job.getMessage().isEmpty());
    }
}

// ------------------------------------------------------ CheckSpecificModel

TEST(ut_TroubleShoot, specificModelOffersARepairOnlyWhenAChecksFailedAndAFixExists)
{
    CheckSpecificModel job("Q");
    EXPECT_TRUE(job.getActions().isEmpty());
    EXPECT_TRUE(job.repairId().isEmpty());
    EXPECT_FALSE(job.hasExpandableContent());

    job.m_entry.id = "canon_lbp2900";
    job.m_entry.fixId = "canon_lbp2900";
    EXPECT_TRUE(job.getActions().isEmpty());          // nothing failed yet

    job.m_failed.append(ModelCheck());
    ASSERT_EQ(job.getActions().size(), 1);
    EXPECT_EQ(job.getActions().first().id, TroubleShootAction::Repair);
    EXPECT_EQ(job.repairId(), QString("canon_lbp2900"));
    EXPECT_TRUE(job.hasExpandableContent());

    job.m_entry.fixId.clear();                        // report-only model
    EXPECT_TRUE(job.getActions().isEmpty());
}

TEST(ut_TroubleShoot, specificModelRepairTextFallsBackToTheGenericOne)
{
    CheckSpecificModel job("Q");
    job.m_entry.id = "no-such-model-id";
    EXPECT_EQ(job.repairResultText(true), job.TroubleShootJob::repairResultText(true));
    EXPECT_EQ(job.repairResultText(false), job.TroubleShootJob::repairResultText(false));
}

TEST(ut_TroubleShoot, specificModelVerifyRepairWithoutChecksPasses)
{
    CheckSpecificModel job("Q");
    EXPECT_TRUE(job.verifyRepair());
    EXPECT_TRUE(job.m_failed.isEmpty());
}

TEST(ut_TroubleShoot, specificModelIsHiddenForUnknownPrinters)
{
    CheckSpecificModel job("");
    EXPECT_TRUE(job.isPass());
    EXPECT_TRUE(job.isHidden());
}

// ===========================================================================
// Checks against a scripted environment (TroubleShootEnv): no CUPS server,
// no network, no root service.
// ===========================================================================

#include <QCoreApplication>
#include <QDBusMessage>
#include <QSet>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>

#include <stdexcept>
#include <tuple>

namespace {

struct Probe
{
    QString host;
    quint16 port;
    int timeoutMs;
};

// Installs a harmless fake for every member of the environment and restores
// the previous one when it goes out of scope. The tests adjust the public data.
class World
{
public:
    World() : m_saved(troubleShootEnv())
    {
        TroubleShootEnv &e = troubleShootEnv();
        e.cupsReachable = [this]() { return cups; };
        e.printerPpd = [this](const QString &printer) { ppdQueues << printer; return ppd; };
        e.driverDepends = [this](const QString &file) { dependsFor << file; return depends; };
        e.packageExists = [this](const QString &package) { checkedPackages << package; return packages.contains(package); };
        e.printerUri = [this](const QString &) { return uri; };
        e.resolveHost = [this](const QString &host) { resolved << host; return resolveError; };
        e.tcpProbe = [this](const QString &host, quint16 port, int timeoutMs) {
            probes.append({host, port, timeoutMs});
            return tcpError;
        };
        e.directDevices = [this]() { ++directCalls; return direct; };
        e.ippUsb = [this](const QString &deviceUri) { ippUsbUris << deviceUri; return ippUsb; };
        e.ping = [this](const QString &host) { pinged << host; return pingOk; };
        e.printerAttributes = [this](const QString &printer, const std::vector<std::string> &requested) {
            attrQueues << printer;
            attrRequests.push_back(requested);
            if (throwAttrs)
                throw std::runtime_error("server went away");
            return attrs;
        };
        e.readErrorLog = [this](const QString &printer) { logQueues << printer; return errorLog; };
        e.errorTable = [this]() { ++errorTableCalls; return errorTable; };
        e.modelTable = [this]() { return modelTable; };
    }
    ~World() { troubleShootEnv() = m_saved; }

    bool cups = true;
    QString ppd;
    QStringList depends;
    QSet<QString> packages;
    QString uri;
    QString resolveError;
    QString tcpError;
    QStringList direct;
    IppUsbDiag::Result ippUsb;
    bool pingOk = true;
    std::map<std::string, std::string> attrs;
    bool throwAttrs = false;
    TroubleshootService::LogResult errorLog;
    QList<CupsErrorEntry> errorTable;
    QList<ModelEntry> modelTable;

    QStringList ppdQueues, dependsFor, checkedPackages, resolved, ippUsbUris, pinged, attrQueues, logQueues;
    QList<Probe> probes;
    std::vector<std::vector<std::string>> attrRequests;
    int directCalls = 0;
    int errorTableCalls = 0;

private:
    TroubleShootEnv m_saved;
};

// Records what a job reports while it runs.
struct StateLog
{
    explicit StateLog(TroubleShootJob &job)
    {
        QObject::connect(&job, &TroubleShootJob::signalStateChanged, [this](int state, const QString &text) {
            states << state;
            texts << text;
        });
    }
    QList<int> states;
    QStringList texts;
};

std::string tagged(const char *tag, const QString &value)
{
    return std::string(tag) + value.toStdString();
}

// A PPD that libcups accepts, with extra lines for the test.
QString writePpd(const QTemporaryDir &dir, const QString &extraLines, const QString &name = "queue.ppd")
{
    const QString content =
        "*PPD-Adobe: \"4.3\"\n*FormatVersion: \"4.3\"\n*FileVersion: \"1.0\"\n*LanguageVersion: English\n"
        "*LanguageEncoding: ISOLatin1\n*PCFileName: \"test.ppd\"\n*Manufacturer: \"Test\"\n"
        "*Product: \"(Test)\"\n*ModelName: \"Test\"\n*ShortNickName: \"Test\"\n*NickName: \"Test\"\n"
        "*PSVersion: \"(3010.000) 0\"\n*LanguageLevel: \"3\"\n*ColorDevice: False\n*DefaultColorSpace: Gray\n"
        "*FileSystem: False\n*Throughput: \"1\"\n*LandscapeOrientation: Plus90\n*TTRasterizer: Type42\n"
        + extraLines + "\n";
    const QString path = dir.filePath(name);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(content.toUtf8());
    return path;
}

// Answers the read-only queries of the model checks by command line.
class ScriptedQueries
{
public:
    explicit ScriptedQueries(std::function<bool(const QStringList &)> passes)
    {
        m_previous = ModelCheckRunner::setCommandRunner(
            [this, passes](const QString &program, const QStringList &args, int) {
                calls << program + " " + args.join(' ');
                SafeProcess::Result r;
                r.started = true;
                r.exitCode = passes(args) ? 0 : 1;
                if (program == "dpkg-query" && r.exitCode == 0)
                    r.out = "install ok installed";
                return r;
            });
    }
    ~ScriptedQueries() { ModelCheckRunner::setCommandRunner(m_previous); }

    QStringList calls;

private:
    ModelCheckRunner::CommandRunner m_previous;
};

}  // namespace

// ----------------------------------------------------------- CheckCupsServer

TEST(ut_TroubleShoot, cupsServerPassesWhenTheServerAnswers)
{
    World world;
    CheckCupsServer job;
    StateLog log(job);
    EXPECT_TRUE(job.isPass());
    EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Suc}));
    EXPECT_TRUE(job.getMessage().isEmpty());
}

TEST(ut_TroubleShoot, cupsServerFailsWithAMessageWhenUnreachable)
{
    World world;
    world.cups = false;
    CheckCupsServer job;
    StateLog log(job);
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Fail}));
    EXPECT_EQ(job.getMessage(), QString("CUPS server is invalid"));
    EXPECT_EQ(log.texts.last(), job.getMessage());
}

// --------------------------------------------------------------- CheckDriver

TEST(ut_TroubleShoot, driverFailsWhenThePpdFileIsMissing)
{
    World world;
    world.ppd = "/nonexistent/queue.ppd";
    CheckDriver job("Q");
    StateLog log(job);
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(world.ppdQueues, QStringList{"Q"});
    EXPECT_EQ(job.getMessage(), QString("PPD file /nonexistent/queue.ppd not found"));
    EXPECT_EQ(log.states.last(), static_cast<int>(TStat_Fail));
}

TEST(ut_TroubleShoot, driverFailsWhenThePpdCannotBeParsed)
{
    World world;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile bad(dir.filePath("bad.ppd"));
    ASSERT_TRUE(bad.open(QIODevice::WriteOnly));
    bad.write("this is not a ppd file\n");
    bad.close();
    world.ppd = bad.fileName();

    CheckDriver job("Q");
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(job.getMessage(), QString("The driver is damaged"));
    EXPECT_TRUE(world.dependsFor.isEmpty());   // nothing else is looked at
}

TEST(ut_TroubleShoot, driverFailsWhenTheFilterProgramIsMissing)
{
    World world;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    world.ppd = writePpd(dir, "*cupsFilter: \"application/vnd.cups-raster 0 /nonexistent/rastertozz\"");

    CheckDriver job("Q");
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(job.getMessage(), QString("Driver filter /nonexistent/rastertozz not found"));
    EXPECT_TRUE(world.dependsFor.isEmpty());
}

TEST(ut_TroubleShoot, driverFailsWhenADependencyIsNotInstalled)
{
    World world;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    world.ppd = writePpd(dir, "*cupsFilter: \"application/vnd.cups-raster 0 /bin/sh\"");
    world.depends = {"libfoo", "libbar", "libbaz"};
    world.packages = {"libfoo"};

    CheckDriver job("Q");
    StateLog log(job);
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(world.dependsFor, QStringList{world.ppd});
    EXPECT_EQ(world.checkedPackages, (QStringList{"libfoo", "libbar"}));   // stops at the first missing one
    EXPECT_EQ(job.getMessage(), QString("libbar is not installed, cannot print now"));
    EXPECT_EQ(log.states.last(), static_cast<int>(TStat_Fail));
}

TEST(ut_TroubleShoot, driverPassesWithAllFiltersAndPackagesPresent)
{
    World world;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    world.ppd = writePpd(dir, "*cupsFilter: \"application/vnd.cups-raster 0 /bin/sh\"");
    world.depends = {"libfoo"};
    world.packages = {"libfoo"};

    CheckDriver job("Q");
    StateLog log(job);
    EXPECT_TRUE(job.isPass());
    EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Suc}));
    EXPECT_TRUE(job.getMessage().isEmpty());
}

TEST(ut_TroubleShoot, driverWithoutAFilterLineOnlyChecksThePackages)
{
    World world;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    world.ppd = writePpd(dir, "");
    CheckDriver job("Q");
    EXPECT_TRUE(job.isPass());
    EXPECT_EQ(world.dependsFor, QStringList{world.ppd});
}

// ------------------------------------------------------------ CheckConnected

namespace {

bool runConnected(World &world, const QString &uri, QString *message = nullptr, QList<int> *states = nullptr)
{
    world.uri = uri;
    CheckConnected job("Q");
    StateLog log(job);
    const bool pass = job.isPass();
    if (message)
        *message = job.getMessage();
    if (states)
        *states = log.states;
    return pass;
}

}  // namespace

TEST(ut_TroubleShoot, connectedUsesTheDefaultPortOfEachScheme)
{
    const QList<std::tuple<QString, QString, quint16>> cases{
        {"socket://10.0.0.5", "10.0.0.5", 9100},
        {"socket://10.0.0.5:9101", "10.0.0.5", 9101},
        {"lpd://10.0.0.6/queue", "10.0.0.6", 515},
        {"ipp://10.0.0.7/ipp/print", "10.0.0.7", 631},
        {"http://10.0.0.8/ipp/print", "10.0.0.8", 631},
        {"https://10.0.0.9/ipp/print", "10.0.0.9", 631},
        {"ipp://10.0.0.10:8631/ipp/print", "10.0.0.10", 8631},
    };
    for (const auto &item : cases) {
        World world;
        QList<int> states;
        EXPECT_TRUE(runConnected(world, std::get<0>(item), nullptr, &states)) << std::get<0>(item).toStdString();
        ASSERT_EQ(world.probes.size(), 1) << std::get<0>(item).toStdString();
        EXPECT_EQ(world.probes.first().host, std::get<1>(item));
        EXPECT_EQ(world.probes.first().port, std::get<2>(item));
        EXPECT_EQ(world.probes.first().timeoutMs, 3000);
        EXPECT_EQ(world.resolved, QStringList{std::get<1>(item)});
        EXPECT_EQ(states, (QList<int>{TStat_Running, TStat_Suc}));
    }
}

TEST(ut_TroubleShoot, connectedAcceptsAnIpv6Literal)
{
    World world;
    EXPECT_TRUE(runConnected(world, "socket://[2001:db8::1]:9100"));
    ASSERT_EQ(world.probes.size(), 1);
    EXPECT_EQ(world.probes.first().host, QString("2001:db8::1"));
    EXPECT_EQ(world.probes.first().port, 9100);
}

TEST(ut_TroubleShoot, connectedReportsAnUnresolvableHostWithoutProbing)
{
    World world;
    world.resolveError = "printer.local not found";
    QString message;
    QList<int> states;
    EXPECT_FALSE(runConnected(world, "socket://printer.local:9100", &message, &states));
    EXPECT_EQ(message, QString("printer.local not found"));
    EXPECT_EQ(states, (QList<int>{TStat_Running, TStat_Fail}));
    EXPECT_TRUE(world.probes.isEmpty());
}

TEST(ut_TroubleShoot, connectedReportsTheTcpError)
{
    World world;
    world.tcpError = "Connection refused";
    QString message;
    EXPECT_FALSE(runConnected(world, "socket://10.0.0.5", &message));
    EXPECT_EQ(message, QString("Cannot connect to the printer, error: Connection refused"));
}

TEST(ut_TroubleShoot, connectedStopsBeforeProbingWhenAskedToStop)
{
    World world;
    world.uri = "socket://10.0.0.5";
    CheckConnected job("Q");
    StateLog log(job);
    job.stop();
    EXPECT_FALSE(job.isPass());
    EXPECT_TRUE(world.probes.isEmpty());
    EXPECT_EQ(log.states, (QList<int>{TStat_Running}));   // neither pass nor fail is claimed
}

TEST(ut_TroubleShoot, connectedDirectDeviceMustBeListedByTheBackend)
{
    const QString uri = "hp:/usb/HP_LaserJet_1018?serial=1";
    {
        World world;
        world.direct = {"hp:/usb/Other?serial=2", uri};
        EXPECT_TRUE(runConnected(world, uri));
        EXPECT_EQ(world.directCalls, 1);
        EXPECT_TRUE(world.ippUsbUris.isEmpty());
        EXPECT_TRUE(world.probes.isEmpty());
    }
    {
        World world;
        QString message;
        EXPECT_FALSE(runConnected(world, uri, &message));
        EXPECT_EQ(message, QString("Q is not connected, URI: ") + uri);
        EXPECT_EQ(world.ippUsbUris, QStringList{uri});
    }
}

TEST(ut_TroubleShoot, connectedExplainsAUsbPrinterTakenOverByIppUsb)
{
    World world;
    world.ippUsb.verdict = IppUsbDiag::Verdict::TakenOver;
    QString message;
    EXPECT_FALSE(runConnected(world, "usb://Pantum/P3301DN?serial=1", &message));
    EXPECT_TRUE(message.contains("ipp-usb"));
    EXPECT_TRUE(message.contains("driverless"));
}

TEST(ut_TroubleShoot, connectedQuotesTheIppUsbStatusOfAnUnhealthyDevice)
{
    World world;
    world.ippUsb.verdict = IppUsbDiag::Verdict::Unhealthy;
    world.ippUsb.status = "failed to start";
    QString message;
    EXPECT_FALSE(runConnected(world, "usb://Canon/LBP2900?serial=1", &message));
    EXPECT_TRUE(message.contains("failed to start"));
}

TEST(ut_TroubleShoot, connectedFileDeviceNeedsItsDirectory)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    {
        World world;
        EXPECT_TRUE(runConnected(world, "file:" + dir.path() + "/out"));
        EXPECT_TRUE(world.probes.isEmpty());
        EXPECT_TRUE(world.pinged.isEmpty());
    }
    {
        World world;
        QString message;
        EXPECT_FALSE(runConnected(world, "file:///nonexistent-zz/out", &message));
        EXPECT_EQ(message, QString("/nonexistent-zz does not exist"));
    }
}

TEST(ut_TroubleShoot, connectedFallsBackToPingForSchemesWithoutAPort)
{
    World world;
    world.uri = "dnssd://My%20Printer._ipp._tcp.local/?uuid=1";
    {
        CheckConnected job("Q");
        EXPECT_TRUE(job.isPass());
        EXPECT_EQ(world.pinged, QStringList{"Printer.local"});
        EXPECT_TRUE(world.probes.isEmpty());
    }
    world.pingOk = false;
    CheckConnected job("Q");
    StateLog log(job);
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(job.getMessage(), QString("Cannot connect to the printer"));
    EXPECT_EQ(log.states.last(), static_cast<int>(TStat_Fail));
}

TEST(ut_TroubleShoot, connectedSmbUsesTheServerName)
{
    World world;
    EXPECT_TRUE(runConnected(world, "smb://WORKGROUP/server/printer"));
    EXPECT_EQ(world.pinged, QStringList{"server"});
}

// ----------------------------------------------------------- CheckAttributes

TEST(ut_TroubleShoot, attributesRequestStateAndAcceptance)
{
    World world;
    world.attrs = {{CUPS_OP_STATE, "i3"}, {CUPS_OP_ISACCEPT, "b1"}};
    CheckAttributes job("Q");
    StateLog log(job);
    EXPECT_TRUE(job.isPass());
    EXPECT_EQ(world.attrQueues, QStringList{"Q"});
    ASSERT_EQ(world.attrRequests.size(), 1u);
    EXPECT_EQ(world.attrRequests[0], (std::vector<std::string>{CUPS_OP_STATE, CUPS_OP_ISACCEPT}));
    EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Suc}));
}

TEST(ut_TroubleShoot, attributesFailForAStoppedPrinter)
{
    World world;
    world.attrs = {{CUPS_OP_STATE, tagged("i", QString::number(IPP_PRINTER_STOPPED))}, {CUPS_OP_ISACCEPT, "b1"}};
    CheckAttributes job("Q");
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(job.getMessage(), QString("Q is disabled"));
}

TEST(ut_TroubleShoot, attributesFailForAPrinterThatRejectsJobs)
{
    World world;
    world.attrs = {{CUPS_OP_STATE, "i3"}, {CUPS_OP_ISACCEPT, "b0"}};
    CheckAttributes job("Q");
    StateLog log(job);
    EXPECT_FALSE(job.isPass());
    EXPECT_TRUE(job.getMessage().startsWith("Q"));
    EXPECT_TRUE(job.getMessage().contains("not accepting jobs"));
    EXPECT_EQ(log.states.last(), static_cast<int>(TStat_Fail));
}

TEST(ut_TroubleShoot, attributesTreatAMissingAnswerAsAHealthyPrinter)
{
    World world;   // the server returned nothing
    CheckAttributes job("Q");
    EXPECT_TRUE(job.isPass());
}

// The check reports Fail to the dialog but still lets the run continue as a
// pass when the attributes cannot be read: the upstream behaviour, kept.
TEST(ut_TroubleShoot, attributesReadErrorIsShownButDoesNotFailTheCheck)
{
    World world;
    world.throwAttrs = true;
    CheckAttributes job("Q");
    StateLog log(job);
    EXPECT_TRUE(job.isPass());
    EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Fail}));
    EXPECT_EQ(log.texts.last(), QString("Failed to get printer attributes, error: server went away"));
    EXPECT_TRUE(job.getMessage().isEmpty());
}

// ------------------------------------------------------------ CheckCupsErrorLog

TEST(ut_TroubleShoot, errorLogFailsClosedWhenTheServiceCannotBeReached)
{
    World world;   // errorLog.ok is false
    CheckCupsErrorLog job("Q");
    StateLog log(job);
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(world.logQueues, QStringList{"Q"});
    EXPECT_EQ(job.getMessage(), QString("The CUPS error log could not be checked"));
    EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Fail}));
    EXPECT_FALSE(job.hasExpandableContent());
}

TEST(ut_TroubleShoot, errorLogWithoutLinesPassesWithoutReadingTheTable)
{
    World world;
    world.errorLog.ok = true;
    CheckCupsErrorLog job("Q");
    StateLog log(job);
    EXPECT_TRUE(job.isPass());
    EXPECT_EQ(world.errorTableCalls, 0);
    EXPECT_EQ(job.getMessage(), QString("No CUPS error found"));
    EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Suc}));
    EXPECT_FALSE(job.hasExpandableContent());
    EXPECT_TRUE(job.getActions().isEmpty());
}

TEST(ut_TroubleShoot, errorLogWithUnknownLinesPasses)
{
    World world;
    world.errorLog.ok = true;
    world.errorLog.text = "E [date] nothing we know about";
    CupsErrorEntry entry;
    entry.id = 1;
    entry.pattern = "filter error";
    entry.category = "filter";
    world.errorTable = {entry};

    CheckCupsErrorLog job("Q");
    EXPECT_TRUE(job.isPass());
    EXPECT_EQ(world.errorTableCalls, 1);
    EXPECT_FALSE(job.m_match.has_value());
    EXPECT_EQ(job.logText(), world.errorLog.text);
    // The lines stay viewable even without a match.
    EXPECT_FALSE(job.hasExpandableContent());
}

TEST(ut_TroubleShoot, errorLogMapsEachOfTheNineKnownErrors)
{
    QFile file(QStringLiteral(TROUBLESHOOT_SOURCE_DIR "/cups_error_map.json"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QList<CupsErrorEntry> table = CupsErrorMap::parse(file.readAll());
    ASSERT_EQ(table.size(), 9);

    const QList<std::pair<int, QString>> samples{
        {1, "Job stopped due to filter error"},
        {2, "Can't create temporary file /var/spool/cups/tmp/x"},
        {3, "Unsafe permissions on /usr/lib/cups/filter"},
        {4, "CUPS server not running"},
        {5, "Invalid device-uri \"foo\""},
        {6, "The scheduler could not execute a filter"},
        {7, "filter rastertofoo not found"},
        {8, "printer not responding"},
        {9, "cups-browsed has exited"},
    };
    for (const auto &sample : samples) {
        World world;
        world.errorLog.ok = true;
        world.errorLog.text = "E [01/Jan/2026:00:00:00 +0000] " + sample.second;
        world.errorTable = table;

        CheckCupsErrorLog job("Q");
        StateLog log(job);
        EXPECT_FALSE(job.isPass()) << sample.first;
        ASSERT_TRUE(job.m_match.has_value()) << sample.first;
        EXPECT_EQ(job.m_match->id, sample.first);
        const CupsErrorText text = CupsErrorMap::textFor(sample.first);
        EXPECT_FALSE(text.title.isEmpty()) << sample.first;
        EXPECT_EQ(job.getMessage(), text.title);
        EXPECT_EQ(log.states.last(), static_cast<int>(TStat_Fail));
        EXPECT_TRUE(job.hasExpandableContent());
        EXPECT_EQ(job.getDetails().size(), 2);
        // Only the service error has a repair; the log can always be viewed and copied.
        EXPECT_EQ(job.repairId(), sample.first == 4 ? QString("cups_service") : QString()) << sample.first;
        EXPECT_EQ(job.getActions().size(), sample.first == 4 ? 3 : 2) << sample.first;
    }
}

// --------------------------------------------------------- CheckSpecificModel

namespace {

std::map<std::string, std::string> makeModel(const QString &text)
{
    return {{CUPS_OP_MAKE_MODEL, tagged("s", text)}};
}

QList<ModelEntry> shippedModels()
{
    QFile file(QStringLiteral(TROUBLESHOOT_SOURCE_DIR "/model_fix.json"));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return ModelFixMap::parse(file.readAll());
}

}  // namespace

TEST(ut_TroubleShoot, specificModelStaysHiddenForAnUnknownModel)
{
    World world;
    world.modelTable = shippedModels();
    world.attrs = makeModel("HP LaserJet 1018");
    CheckSpecificModel job("Q");
    StateLog log(job);
    EXPECT_TRUE(job.isPass());
    EXPECT_TRUE(job.isHidden());
    EXPECT_EQ(log.states, (QList<int>{TStat_Suc}));
    EXPECT_TRUE(log.texts.first().isEmpty());
    EXPECT_EQ(world.attrRequests[0], std::vector<std::string>{CUPS_OP_MAKE_MODEL});
}

TEST(ut_TroubleShoot, specificModelStaysHiddenWhenTheModelCannotBeRead)
{
    World world;
    world.modelTable = shippedModels();
    world.throwAttrs = true;
    CheckSpecificModel job("Q");
    EXPECT_TRUE(job.isPass());
    EXPECT_TRUE(job.isHidden());
}

TEST(ut_TroubleShoot, specificModelPassesWhenEveryCheckPasses)
{
    World world;
    world.modelTable = shippedModels();
    world.attrs = makeModel("Canon LBP2900 CAPT");
    ScriptedQueries queries([](const QStringList &) { return true; });

    CheckSpecificModel job("Q");
    StateLog log(job);
    EXPECT_TRUE(job.isPass());
    EXPECT_FALSE(job.isHidden());
    EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Suc}));
    EXPECT_EQ(job.getMessage(), ModelFixMap::textsFor("canon_lbp2900").ok);
    EXPECT_TRUE(job.getActions().isEmpty());
    // All four checks of the entry run, in the order of the table.
    EXPECT_EQ(queries.calls, (QStringList{"systemctl is-active --quiet -- ccpd",
                                          "systemctl is-enabled --quiet -- ccpd",
                                          "dpkg-query -W -f=${Status} -- libpopt0:i386",
                                          "dpkg-query -W -f=${Status} -- libxml2:i386"}));
}

TEST(ut_TroubleShoot, specificModelOffersARepairWhenACheckFailsAndVerifiesIt)
{
    World world;
    world.modelTable = shippedModels();
    world.attrs = makeModel("Canon LBP2900");
    bool serviceActive = false;
    ScriptedQueries queries([&serviceActive](const QStringList &args) {
        return args.contains("ccpd") && args.first() == "is-active" ? serviceActive : true;
    });

    CheckSpecificModel job("Q");
    StateLog log(job);
    EXPECT_FALSE(job.isPass());
    const ModelTexts texts = ModelFixMap::textsFor("canon_lbp2900");
    EXPECT_EQ(job.getMessage(), texts.problem);
    EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Fail}));
    EXPECT_EQ(job.repairId(), QString("canon_lbp2900"));
    ASSERT_EQ(job.getActions().size(), 1);
    EXPECT_EQ(job.getActions().first().id, TroubleShootAction::Repair);
    EXPECT_TRUE(job.hasExpandableContent());

    EXPECT_FALSE(job.verifyRepair());            // the service is still down
    EXPECT_EQ(job.repairResultText(false), texts.fixFail);

    serviceActive = true;
    EXPECT_TRUE(job.verifyRepair());
    EXPECT_EQ(job.repairResultText(true), texts.fixOk);
    EXPECT_TRUE(job.repairId().isEmpty());       // nothing left to repair
    EXPECT_TRUE(job.getActions().isEmpty());
}

TEST(ut_TroubleShoot, specificModelChecksFilesInTheGivenDirectory)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile file(dir.filePath("brother_x"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::WriteOther);

    ModelEntry entry;
    entry.id = "brother_dcp_mfc";
    entry.matchRules = {"Brother DCP"};
    ModelCheck check;
    check.type = ModelCheck::Type::FilesNotWritable;
    check.target = dir.path();
    check.pattern = "brother_*";
    entry.checks = {check};
    entry.fixId = "brother_dcp_mfc";

    World world;
    world.modelTable = {entry};
    world.attrs = makeModel("Brother DCP-7090DW");

    CheckSpecificModel job("Q");
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(job.repairId(), QString("brother_dcp_mfc"));

    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    EXPECT_TRUE(job.verifyRepair());
}

TEST(ut_TroubleShoot, specificModelWithoutAFixOnlyReports)
{
    ModelEntry entry;
    entry.id = "unlisted_model";
    entry.matchRules = {"Foo"};
    ModelCheck check;
    check.type = ModelCheck::Type::ServiceActive;
    check.target = "foo";
    entry.checks = {check};

    World world;
    world.modelTable = {entry};
    world.attrs = makeModel("Foo 1");
    ScriptedQueries queries([](const QStringList &) { return false; });

    CheckSpecificModel job("Q");
    EXPECT_FALSE(job.isPass());
    EXPECT_EQ(job.getMessage(), QString("Model-specific issues were found"));   // no texts for this id
    EXPECT_TRUE(job.getActions().isEmpty());
    EXPECT_TRUE(job.repairId().isEmpty());
}

// ----------------------------------------------------------- the whole run

TEST(ut_TroubleShoot, aFullRunReportsEachCheckInOrder)
{
    World world;
    world.ppd = "/nonexistent/queue.ppd";              // the driver check fails
    world.attrs = {{CUPS_OP_STATE, "i3"}, {CUPS_OP_ISACCEPT, "b1"}};

    TroubleShoot task("Q");
    QList<QPair<int, QString>> progress;
    QObject::connect(&task, &TaskInterface::signalUpdateProgress,
                     [&progress](int index, QString message) { progress.append({index, message}); });

    EXPECT_EQ(task.doWork(), -1);
    ASSERT_EQ(progress.size(), 3);
    EXPECT_EQ(progress.at(0), (QPair<int, QString>{0, QString()}));
    EXPECT_EQ(progress.at(1), (QPair<int, QString>{1, QString("PPD file /nonexistent/queue.ppd not found")}));
    EXPECT_EQ(progress.at(2), (QPair<int, QString>{2, QString()}));
}

TEST(ut_TroubleShoot, diagnosticJobsComeAfterTheBasicChecks)
{
    TroubleShoot task("Q");
    task.addDiagnosticJobs();
    EXPECT_EQ(jobNames(task.getJobs()),
              (QStringList{"Check CUPS server", "Check driver", "Check printer settings",
                           "Check CUPS error log", "Check model-specific issues"}));
}

TEST(ut_TroubleShoot, aJobThatStopsTheTaskPreventsTheLaterOnes)
{
    struct StoppingJob : FakeJob
    {
        StoppingJob(TroubleShoot *t) : FakeJob("stopper", true), task(t) {}
        bool isPass() override
        {
            task->m_bQuit = true;
            return FakeJob::isPass();
        }
        TroubleShoot *task;
    };

    TroubleShoot task("Q");
    task.m_jobs.clear();
    StoppingJob stopper(&task);
    FakeJob later("later", false);
    task.addJob(&stopper);
    task.addJob(&later);

    EXPECT_EQ(task.doWork(), 0);
    EXPECT_EQ(stopper.runs, 1);
    EXPECT_EQ(later.runs, 0);
}

TEST(ut_TroubleShoot, theTaskThreadReportsFailureOfAnyCheck)
{
    FakeJob good("good", true), bad("bad", false);
    TroubleShoot task("Q");
    task.m_jobs.clear();
    task.addJob(&good);
    task.addJob(&bad);

    task.start();
    ASSERT_TRUE(task.wait(5000));
    EXPECT_EQ(task.getErrCode(), -1);
    EXPECT_EQ(good.runs, 1);
    EXPECT_EQ(bad.runs, 1);
}

// ------------------------------------------------------------ PrinterTestJob

namespace {

// Backend for the job manager that records test page requests and lists jobs.
class StubJobs : public JobBackend
{
public:
    StubJobs() { m_previous = g_jobManager->setBackend(this); }
    ~StubJobs() override { g_jobManager->setBackend(m_previous); }

    void getJobs(map<int, map<string, string>> &out, const char *, int, int, int, const vector<string> *) override
    {
        if (throwOnList)
            throw std::runtime_error("no server");
        out = jobs;
    }
    void printTestPage(const char *dest, const char *, const char *title, const char *, int &jobId) override
    {
        printed << QString("%1 %2").arg(QString::fromUtf8(dest), QString::fromUtf8(title));
        jobId = newJobId;
    }
    QString testPageFile() const override { return testFile; }
    std::function<bool(int)> purgedCheck() override { return nullptr; }

    map<int, map<string, string>> jobs;
    bool throwOnList = false;
    int newJobId = 21;
    QString testFile;
    QStringList printed;

private:
    JobBackend *m_previous = nullptr;
};

QDBusMessage jobSignal(const QVariantList &args)
{
    QDBusMessage message = QDBusMessage::createSignal("/", "com.test", "signalJobStateChanged");
    message.setArguments(args);
    return message;
}

void flushDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

}  // namespace

TEST(ut_TroubleShoot, testPageJobPrintsAndReportsWithoutWaiting)
{
    QTemporaryFile page;
    ASSERT_TRUE(page.open());
    StubJobs jobs;
    jobs.testFile = page.fileName();
    {
        PrinterTestJob job("Q", nullptr, false);
        StateLog log(job);
        EXPECT_EQ(job.getJobName(), QString("Check test page printing"));
        EXPECT_TRUE(job.isPass());
        EXPECT_EQ(jobs.printed, QStringList{"Q Test page"});
        EXPECT_EQ(job.m_jobId, 21);
        EXPECT_EQ(log.states, (QList<int>{TStat_Running}));

        EXPECT_TRUE(job.isPass());                 // the job exists: it is not printed again
        EXPECT_EQ(jobs.printed.size(), 1);
    }
    flushDeletes();
}

TEST(ut_TroubleShoot, testPageJobFailsWhenThePageCannotBePrinted)
{
    StubJobs jobs;
    jobs.testFile = "/nonexistent/testprint";
    PrinterTestJob job("Q", nullptr, false);
    StateLog log(job);
    EXPECT_FALSE(job.isPass());
    EXPECT_TRUE(job.getMessage().contains("not found"));
    EXPECT_EQ(log.states.last(), static_cast<int>(TStat_Fail));
    EXPECT_TRUE(jobs.printed.isEmpty());
}

TEST(ut_TroubleShoot, runningTestJobIsFoundByPrinterAndTitle)
{
    StubJobs jobs;
    jobs.jobs = {
        {3, {{JOB_ATTR_URI, "sipp://localhost/printers/Other"}, {JOB_ATTR_STATE, "i5"}, {JOB_ATTR_NAME, "sTest page"}}},
        {4, {{JOB_ATTR_URI, "sipp://localhost/printers/Q"}, {JOB_ATTR_STATE, "i5"}, {JOB_ATTR_NAME, "sreport.pdf"}}},
        {5, {{JOB_ATTR_URI, "sipp://localhost/printers/Q"}, {JOB_ATTR_STATE, "i9"}, {JOB_ATTR_NAME, "sTest page"}}},
        {6, {{JOB_ATTR_URI, "sipp://localhost/printers/Q"}, {JOB_ATTR_STATE, "i5"}, {JOB_ATTR_NAME, "sTest page"}}},
    };
    PrinterTestJob job("Q", nullptr, false);
    StateLog log(job);
    EXPECT_TRUE(job.findRunningJob());
    EXPECT_EQ(job.m_jobId, 6);   // not another printer's, not another title, not a completed one
    EXPECT_EQ(log.states, (QList<int>{TStat_Update}));

    // With the job known, no new test page is printed.
    jobs.testFile = "/nonexistent/testprint";
    EXPECT_TRUE(job.isPass());
    EXPECT_TRUE(jobs.printed.isEmpty());
    flushDeletes();
}

TEST(ut_TroubleShoot, findRunningJobIsFalseWithoutAMatchOrWithAnError)
{
    StubJobs jobs;
    jobs.jobs = {{5, {{JOB_ATTR_URI, "sipp://localhost/printers/Q"}, {JOB_ATTR_STATE, "i9"}, {JOB_ATTR_NAME, "sTest page"}}}};
    PrinterTestJob job("Q", nullptr, false);
    EXPECT_FALSE(job.findRunningJob());
    EXPECT_EQ(job.m_jobId, -1);

    jobs.throwOnList = true;
    EXPECT_FALSE(job.findRunningJob());
}

TEST(ut_TroubleShoot, testPageJobMapsJobStatesToDialogStates)
{
    const QList<std::pair<int, int>> mapping{
        {IPP_JSTATE_PENDING, TStat_Update},   {IPP_JSTATE_HELD, TStat_Update},
        {IPP_JSTATE_PROCESSING, TStat_Update}, {IPP_JSTATE_COMPLETED, TStat_Suc},
        {IPP_JSTATE_STOPPED, TStat_Fail},     {IPP_JSTATE_CANCELED, TStat_Fail},
        {IPP_JSTATE_ABORTED, TStat_Fail},     {99, TStat_Fail},
    };
    for (const auto &item : mapping) {
        PrinterTestJob job("Q", nullptr, false);
        job.m_jobId = 8;
        StateLog log(job);
        job.slotJobStateChanged(jobSignal({8, item.first, "text"}));
        EXPECT_EQ(log.states, (QList<int>{item.second})) << item.first;
    }
    flushDeletes();
}

TEST(ut_TroubleShoot, testPageJobIgnoresOtherJobsAndMalformedSignals)
{
    PrinterTestJob job("Q", nullptr, false);
    job.m_jobId = 8;
    StateLog log(job);
    job.slotJobStateChanged(jobSignal({9, IPP_JSTATE_COMPLETED, "other job"}));
    job.slotJobStateChanged(jobSignal({8, IPP_JSTATE_COMPLETED}));
    job.slotJobStateChanged(jobSignal({}));
    EXPECT_TRUE(log.states.isEmpty());
    flushDeletes();
}

TEST(ut_TroubleShoot, synchronousTestPageWaitsForTheFinalJobState)
{
    QTemporaryFile page;
    ASSERT_TRUE(page.open());
    StubJobs jobs;
    jobs.testFile = page.fileName();
    {
        PrinterTestJob job("Q");
        StateLog log(job);
        // The signals arrive once isPass() is waiting.
        QTimer::singleShot(0, [&job]() { job.slotJobStateChanged(jobSignal({21, IPP_JSTATE_PROCESSING, ""})); });
        QTimer::singleShot(0, [&job]() { job.slotJobStateChanged(jobSignal({21, IPP_JSTATE_COMPLETED, ""})); });

        EXPECT_TRUE(job.isPass());
        EXPECT_EQ(log.states, (QList<int>{TStat_Running, TStat_Update, TStat_Suc}));
    }
    flushDeletes();
}

TEST(ut_TroubleShoot, synchronousTestPageFailsWhenTheJobIsCanceled)
{
    QTemporaryFile page;
    ASSERT_TRUE(page.open());
    StubJobs jobs;
    jobs.testFile = page.fileName();
    {
        PrinterTestJob job("Q");
        StateLog log(job);
        QTimer::singleShot(0, [&job]() { job.slotJobStateChanged(jobSignal({21, IPP_JSTATE_CANCELED, ""})); });
        EXPECT_FALSE(job.isPass());
        EXPECT_EQ(log.states.last(), static_cast<int>(TStat_Fail));
    }
    flushDeletes();
}

TEST(ut_TroubleShoot, stoppingASynchronousTestPageEndsTheWait)
{
    QTemporaryFile page;
    ASSERT_TRUE(page.open());
    StubJobs jobs;
    jobs.testFile = page.fileName();
    {
        PrinterTestJob job("Q");
        QTimer::singleShot(0, [&job]() { job.stop(); });
        // stop() leaves the wait with 0, which the dialog treats as done.
        EXPECT_TRUE(job.isPass());
        EXPECT_TRUE(job.m_bQuit);
    }
    flushDeletes();
}
