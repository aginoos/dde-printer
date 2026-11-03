// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Orchestration of CanonI386Repair::run(): which tools are called with which
// arguments, the RepairCode of every success and failure branch, the lock
// retries and the rollback of the i386 architecture. All tools are fake
// scripts (see faketools.h); nothing touches the real package manager.

#include <gtest/gtest.h>

#include "faketools.h"

#include "../../src/Common/repaircodes.h"
#include "canoni386repair.h"
#include "repairjobs.h"

namespace {

const char kLockOutput[] = "E: Could not get lock /var/lib/dpkg/lock-frontend. It is held by process 42";

// What the fake system answers; the bodies are /bin/sh code.
struct Scenario
{
    bool archEnabled = false;
    bool libsInstalledAtStart = false;
    QString addBody = "exit 0";
    QString updateBody = "exit 0";
    QString installBody = "exit 0";
    QString removeBody = "exit 0";
    QString allQueryOutput;     // lines printed for the "status architecture" query
    bool allQueryFails = false;
    bool libsPresentAfterInstall = true;
    bool ccpdActive = true;
};

QString printLines(const QStringList &lines)
{
    if (lines.isEmpty())
        return ":";
    QStringList parts;
    for (const QString &line : lines)
        parts << "printf '%s\\n' '" + line + "'";
    return parts.join("; ");
}

class ut_CanonI386Run : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tools.isValid());
        CanonI386Repair::setLockRetryDelayMs(10);
    }
    void TearDown() override { CanonI386Repair::setLockRetryDelayMs(-1); }

    void install(const Scenario &s, bool withSystemd = true, bool withSystemctl = true)
    {
        const QString d = m_tools.dir();
        // --print-foreign-architectures: the architecture counts as enabled when a line is exactly "i386"
        m_tools.add("dpkg", s.archEnabled ? "echo i386" : "exit 0");

        const QString libsAfter = s.libsPresentAfterInstall ? "ii" : "un";
        QString query = "case \"$*\" in\n*Architecture*)\n";
        query += s.allQueryFails ? "exit 1;;\n" : printLines(s.allQueryOutput.split('\n', Qt::SkipEmptyParts)) + "; exit 0;;\n";
        query += "*)\nif [ -f '" + d + "/installed' ]; then " + printLines({libsAfter, libsAfter})
            + "; elif [ " + (s.libsInstalledAtStart ? "1" : "0") + " = 1 ]; then " + printLines({"ii", "ii"})
            + "; else " + printLines({"un", "un"}) + "; fi;;\nesac";
        m_tools.add("dpkg-query", query);

        if (withSystemd) {
            const QString mark = "touch '" + d + "/installed'; ";
            m_tools.add("systemd-run",
                        "case \" $* \" in\n"
                        "*' --add-architecture '*) " + s.addBody + ";;\n"
                        "*' --remove-architecture '*) " + s.removeBody + ";;\n"
                        "*' update '*) " + s.updateBody + ";;\n"
                        "*' install '*) " + mark + s.installBody + ";;\n"
                        "esac");
        }
        m_tools.add("apt-get", "exit 0");
        if (withSystemctl)
            m_tools.add("systemctl", "case \"$1\" in is-active) exit " + QString(s.ccpdActive ? "0" : "3") + ";; esac; exit 0");
    }

    int runRepair()
    {
        QObject owner;
        const auto waited = waitForResult<int>([&](std::function<void(int)> cb) {
            CanonI386Repair::run(&owner, cb);
        });
        EXPECT_EQ(waited.calls, 1) << "done must be called exactly once";
        return waited.value.value_or(-99);
    }

    // systemd-run invocations as "<apt-or-dpkg-op>" keywords, in order.
    QStringList operations() const
    {
        QStringList ops;
        for (const QStringList &call : m_tools.callsOf("systemd-run")) {
            const QString joined = " " + call.join(' ') + " ";
            for (const char *op : {"--add-architecture", "--remove-architecture", " update ", " install "}) {
                if (joined.contains(QString::fromLatin1(op))) {
                    ops << QString::fromLatin1(op).trimmed();
                    break;
                }
            }
        }
        return ops;
    }

    FakeTools m_tools;
};

}  // namespace

TEST_F(ut_CanonI386Run, nothingToDoOnlyStartsCcpd)
{
    Scenario s;
    s.archEnabled = true;
    s.libsInstalledAtStart = true;
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::Ok);
    EXPECT_TRUE(m_tools.callsOf("systemd-run").isEmpty());
    EXPECT_EQ(m_tools.callsOf("systemctl"),
              (QList<QStringList>{{"enable", "--now", "ccpd.service"}, {"is-active", "--quiet", "ccpd.service"}}));
}

TEST_F(ut_CanonI386Run, ccpdNotActiveAfterwardsIsAPlainFailure)
{
    Scenario s;
    s.archEnabled = true;
    s.libsInstalledAtStart = true;
    s.ccpdActive = false;
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::Failed);
}

TEST_F(ut_CanonI386Run, missingSystemctlIsAPlainFailure)
{
    Scenario s;
    s.archEnabled = true;
    s.libsInstalledAtStart = true;
    install(s, true, false);
    EXPECT_EQ(runRepair(), RepairCode::Failed);
}

TEST_F(ut_CanonI386Run, enabledArchitectureWithMissingLibrariesSkipsAddArchitecture)
{
    Scenario s;
    s.archEnabled = true;
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::Ok);
    EXPECT_EQ(operations(), (QStringList{"update", "install"}));
}

TEST_F(ut_CanonI386Run, fullRepairRunsEveryStepAsATransientUnit)
{
    Scenario s;
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::Ok);
    EXPECT_EQ(operations(), (QStringList{"--add-architecture", "update", "install"}));

    const QString dpkg = m_tools.pathOf("dpkg");
    const QString apt = m_tools.pathOf("apt-get");
    const QList<QStringList> units = m_tools.callsOf("systemd-run");
    ASSERT_EQ(units.size(), 3);
    EXPECT_EQ(units.at(0), CanonI386Repair::transientUnitArgs(dpkg, {"--add-architecture", "i386"},
                                                              RepairJobs::kStepTimeoutMs / 1000));
    EXPECT_EQ(units.at(1), CanonI386Repair::transientUnitArgs(apt, CanonI386Repair::aptUpdateArgs(),
                                                              CanonI386Repair::kUpdateTimeoutMs / 1000));
    EXPECT_EQ(units.at(2), CanonI386Repair::transientUnitArgs(
                               apt, RepairJobs::aptInstallArgs(RepairJobs::canonI386Packages()),
                               RepairJobs::kInstallTimeoutMs / 1000));
    // no rollback on success, ccpd is started last
    EXPECT_EQ(m_tools.toolOrder().last(), QString("systemctl"));
}

TEST_F(ut_CanonI386Run, addArchitectureFailureReportsItWithoutRollback)
{
    Scenario s;
    s.addBody = "echo 'dpkg: error'; exit 1";
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::ArchitectureFailed);
    EXPECT_EQ(operations(), (QStringList{"--add-architecture"}));
}

TEST_F(ut_CanonI386Run, lockedAddArchitectureRetriesThenReportsBusy)
{
    Scenario s;
    s.addBody = QString("echo '%1'; exit 100").arg(kLockOutput);
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::PackageManagerBusy);
    // first try plus kLockRetries retries
    EXPECT_EQ(operations().size(), 1 + CanonI386Repair::kLockRetries);
}

TEST_F(ut_CanonI386Run, lockReleasedDuringRetriesLetsTheRepairSucceed)
{
    Scenario s;
    const QString counter = m_tools.dir() + "/update-count";
    s.updateBody = QString("n=$(cat '%1' 2>/dev/null || echo 0); n=$((n+1)); echo $n > '%1'; "
                           "if [ $n -lt 3 ]; then echo '%2'; exit 100; fi; exit 0")
                       .arg(counter, kLockOutput);
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::Ok);
    EXPECT_EQ(operations(), (QStringList{"--add-architecture", "update", "update", "update", "install"}));
}

TEST_F(ut_CanonI386Run, updateFailureRollsBackTheArchitectureItAdded)
{
    Scenario s;
    s.updateBody = "echo 'E: Failed to fetch'; exit 100";
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::UpdateFailed);
    EXPECT_EQ(operations(), (QStringList{"--add-architecture", "update", "--remove-architecture"}));
    EXPECT_EQ(m_tools.callsOf("systemd-run").last().mid(m_tools.callsOf("systemd-run").last().size() - 2),
              (QStringList{"--remove-architecture", "i386"}));
}

TEST_F(ut_CanonI386Run, updateStillLockedAfterRetriesIsBusyAndRollsBack)
{
    Scenario s;
    s.updateBody = QString("echo '%1'; exit 100").arg(kLockOutput);
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::PackageManagerBusy);
    EXPECT_EQ(operations().count("update"), 1 + CanonI386Repair::kLockRetries);
    EXPECT_EQ(operations().last(), QString("--remove-architecture"));
}

TEST_F(ut_CanonI386Run, installFailureRollsBack)
{
    Scenario s;
    s.installBody = "exit 100";
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::InstallFailed);
    EXPECT_EQ(operations(), (QStringList{"--add-architecture", "update", "install", "--remove-architecture"}));
}

TEST_F(ut_CanonI386Run, installLockedAfterRetriesIsBusy)
{
    Scenario s;
    s.installBody = QString("echo '%1'; exit 100").arg(kLockOutput);
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::PackageManagerBusy);
    EXPECT_EQ(operations().count("install"), 1 + CanonI386Repair::kLockRetries);
}

TEST_F(ut_CanonI386Run, installExitStatusAloneIsNotTrusted)
{
    Scenario s;
    s.libsPresentAfterInstall = false;   // apt-get "succeeds" but dpkg does not list the libraries
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::InstallFailed);
    EXPECT_EQ(operations().last(), QString("--remove-architecture"));
}

TEST_F(ut_CanonI386Run, rollbackIsSkippedWhenI386PackagesRemain)
{
    Scenario s;
    s.updateBody = "exit 100";
    s.allQueryOutput = "ii i386\nii amd64";
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::UpdateFailed);
    EXPECT_EQ(operations(), (QStringList{"--add-architecture", "update"}));
}

TEST_F(ut_CanonI386Run, rollbackIsSkippedWhenThePackageDatabaseCannotBeRead)
{
    Scenario s;
    s.updateBody = "exit 100";
    s.allQueryFails = true;
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::UpdateFailed);
    EXPECT_EQ(operations(), (QStringList{"--add-architecture", "update"}));
}

TEST_F(ut_CanonI386Run, failedRollbackStillReportsTheOriginalCode)
{
    Scenario s;
    s.updateBody = "exit 100";
    s.removeBody = "exit 2";
    install(s);
    EXPECT_EQ(runRepair(), RepairCode::UpdateFailed);
}

TEST_F(ut_CanonI386Run, withoutSystemdRunNothingIsChanged)
{
    Scenario s;
    install(s, false);
    EXPECT_EQ(runRepair(), RepairCode::ArchitectureFailed);
    EXPECT_TRUE(m_tools.callsOf("apt-get").isEmpty());
}

TEST_F(ut_CanonI386Run, withoutDpkgTheArchitectureCountsAsMissing)
{
    FakeTools bare;   // replaces the resolver of m_tools: nothing is installed
    QObject owner;
    const auto waited = waitForResult<int>([&](std::function<void(int)> cb) { CanonI386Repair::run(&owner, cb); });
    ASSERT_EQ(waited.calls, 1);
    EXPECT_EQ(*waited.value, RepairCode::ArchitectureFailed);
    EXPECT_TRUE(bare.calls().isEmpty());
}

TEST(ut_CanonI386RunDelay, SmokeNegativeDelayRestoresTheDefault)
{
    CanonI386Repair::setLockRetryDelayMs(5);
    CanonI386Repair::setLockRetryDelayMs(-1);
    SUCCEED();   // the default is only observable by waiting kLockRetryDelayMs
}
