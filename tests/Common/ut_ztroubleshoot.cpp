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
