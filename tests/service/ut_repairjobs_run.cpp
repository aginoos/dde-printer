// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

// RepairJobs::run(): the cups_service escalation, the Brother filter
// permission repair (in a temporary directory, never /usr/lib/cups/filter)
// and the dispatch to the other repairs.

#include <gtest/gtest.h>

#include "faketools.h"

#include "../../src/Common/repaircodes.h"
#include "repairjobs.h"

#include <QFile>
#include <QFileInfo>

namespace {

int runFix(const QString &id)
{
    QObject owner;
    const auto waited = waitForResult<int>([&](std::function<void(int)> cb) { RepairJobs::run(&owner, id, cb); });
    EXPECT_EQ(waited.calls, 1) << "done must be called exactly once";
    return waited.value.value_or(-99);
}

const QList<QStringList> kRestartThenCheck = {
    {"restart", "cups.service"}, {"is-active", "--quiet", "cups.service"}};

}  // namespace

TEST(ut_RepairJobsCups, healthyAfterRestartStopsThere)
{
    FakeTools tools;
    ASSERT_TRUE(tools.isValid());
    tools.add("systemctl", "exit 0");
    EXPECT_EQ(runFix("cups_service"), RepairCode::Ok);
    EXPECT_EQ(tools.callsOf("systemctl"), kRestartThenCheck);
}

TEST(ut_RepairJobsCups, failedRestartEscalatesAndSucceedsOnTheSecondCheck)
{
    FakeTools tools;
    ASSERT_TRUE(tools.isValid());
    const QString counter = tools.dir() + "/checks";
    tools.add("systemctl",
              QString("if [ \"$1\" = is-active ]; then n=$(cat '%1' 2>/dev/null || echo 0); n=$((n+1)); "
                      "echo $n > '%1'; [ $n -ge 2 ]; exit $?; fi; exit 1").arg(counter));
    EXPECT_EQ(runFix("cups_service"), RepairCode::Ok);
    EXPECT_EQ(tools.callsOf("systemctl"),
              (QList<QStringList>{{"restart", "cups.service"},
                                  {"is-active", "--quiet", "cups.service"},
                                  {"reset-failed", "cups.service"},
                                  {"enable", "cups.service"},
                                  {"restart", "cups.service"},
                                  {"is-active", "--quiet", "cups.service"}}));
}

TEST(ut_RepairJobsCups, serviceThatNeverComesBackIsAFailure)
{
    FakeTools tools;
    ASSERT_TRUE(tools.isValid());
    tools.add("systemctl", "exit 3");
    EXPECT_EQ(runFix("cups_service"), RepairCode::Failed);
    EXPECT_EQ(tools.callsOf("systemctl").size(), 6);
}

TEST(ut_RepairJobsCups, missingSystemctlIsAFailureWithoutAnyCommand)
{
    FakeTools tools;
    ASSERT_TRUE(tools.isValid());
    EXPECT_EQ(runFix("cups_service"), RepairCode::Failed);
    EXPECT_TRUE(tools.calls().isEmpty());
}

TEST(ut_RepairJobsDispatch, unknownIdRunsNothing)
{
    FakeTools tools;
    ASSERT_TRUE(tools.isValid());
    tools.add("systemctl", "exit 0");
    EXPECT_EQ(runFix("cups_service; reboot"), RepairCode::Failed);
    EXPECT_TRUE(tools.calls().isEmpty());
}

TEST(ut_RepairJobsDispatch, canonRepairReportsItsSpecificCode)
{
    FakeTools tools;   // nothing installed: the architecture cannot be added
    ASSERT_TRUE(tools.isValid());
    EXPECT_EQ(runFix("canon_lbp2900"), RepairCode::ArchitectureFailed);
}

class ut_RepairJobsBrother : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_dir.isValid());
        RepairJobs::setFilterDirectory(m_dir.path());
    }
    void TearDown() override { RepairJobs::setFilterDirectory(QString()); }

    QString create(const QString &name, QFile::Permissions permissions)
    {
        const QString path = m_dir.filePath(name);
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();
        EXPECT_TRUE(QFile::setPermissions(path, permissions));
        return path;
    }

    QFile::Permissions permissionsOf(const QString &path) const { return QFileInfo(path).permissions(); }

    QTemporaryDir m_dir;
};

TEST_F(ut_RepairJobsBrother, removesGroupAndOtherWriteFromBrotherFiltersOnly)
{
    const QFile::Permissions loose = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup
        | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther;
    const QString brother = create("brother_lpdwrapper_DCP7090DW", loose);
    const QString other = create("rastertopwg", loose);
    const QString oddName = create("brother_bad name", loose);

    EXPECT_EQ(runFix("brother_dcp_mfc"), RepairCode::Ok);

    const QFile::Permissions fixed = permissionsOf(brother);
    EXPECT_FALSE(fixed & QFile::WriteGroup);
    EXPECT_FALSE(fixed & QFile::WriteOther);
    EXPECT_TRUE(fixed & QFile::WriteOwner);
    EXPECT_TRUE(fixed & QFile::ExeOwner);
    EXPECT_TRUE(fixed & QFile::ReadGroup);
    EXPECT_EQ(permissionsOf(other), loose);     // not a Brother filter
    EXPECT_EQ(permissionsOf(oddName), loose);   // name fails the whitelist
}

TEST_F(ut_RepairJobsBrother, symbolicLinksAreNeverFollowed)
{
    const QFile::Permissions loose = QFile::ReadOwner | QFile::WriteOwner | QFile::WriteGroup | QFile::WriteOther;
    const QString target = create("target_file", loose);
    ASSERT_TRUE(QFile::link(target, m_dir.filePath("brother_link")));

    EXPECT_EQ(runFix("brother_dcp_mfc"), RepairCode::Ok);
    EXPECT_EQ(permissionsOf(target), loose);
}

TEST_F(ut_RepairJobsBrother, alreadyTightFilesStayAsTheyAre)
{
    const QFile::Permissions tight = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup
        | QFile::ReadOther;
    const QString path = create("brother_ok", tight);
    EXPECT_EQ(runFix("brother_dcp_mfc"), RepairCode::Ok);
    EXPECT_EQ(permissionsOf(path), tight);
}

TEST_F(ut_RepairJobsBrother, emptyOrMissingDirectoryIsSuccess)
{
    EXPECT_EQ(runFix("brother_dcp_mfc"), RepairCode::Ok);
    RepairJobs::setFilterDirectory(m_dir.filePath("does-not-exist"));
    EXPECT_EQ(runFix("brother_dcp_mfc"), RepairCode::Ok);
}

TEST_F(ut_RepairJobsBrother, subdirectoriesAreIgnored)
{
    ASSERT_TRUE(QDir(m_dir.path()).mkdir("brother_dir"));
    EXPECT_EQ(runFix("brother_dcp_mfc"), RepairCode::Ok);
}
