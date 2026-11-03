// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QDateTime>
#include <QFile>
#include <QTemporaryFile>

#include <unistd.h>

#include "../service/faketools.h"
#include "joblogreader.h"
#include "repairjobs.h"

TEST(ut_JobLogReader, ParseLpstatKeepsOnlyRequestedPrinter)
{
    const QString output =
        "Q-12                    alice             1024   Mon 03 Nov 2026 10:00:00 AM CST\n"
        "Q-13                    bob               2048   Mon 03 Nov 2026 10:05:00 AM CST\n"
        "Q2-14                   alice             2048   Mon 03 Nov 2026 10:06:00 AM CST\n"
        "Q-x                     alice             1      Mon\n"
        "garbage\n"
        "\n";
    const auto jobs = JobLogReader::parseLpstatJobs(output, "Q");
    ASSERT_EQ(jobs.size(), 2);
    EXPECT_EQ(jobs.at(0).id, 12);
    EXPECT_EQ(jobs.at(1).user, QString("bob"));
}

TEST(ut_JobLogReader, NewestJobIsOwnedByCaller)
{
    const QList<JobLogReader::JobEntry> jobs = {{12, "alice"}, {13, "bob"}, {9, "alice"}};
    EXPECT_EQ(JobLogReader::newestJobOf(jobs, "alice"), 12);
    EXPECT_EQ(JobLogReader::newestJobOf(jobs, "bob"), 13);
    EXPECT_EQ(JobLogReader::newestJobOf(jobs, "carol"), 0);
    EXPECT_EQ(JobLogReader::newestJobOf(jobs, ""), 0);
    EXPECT_EQ(JobLogReader::newestJobOf({}, "alice"), 0);
}

TEST(ut_JobLogReader, ExtractOnlyMatchingJobLines)
{
    const QByteArray log =
        "E [03/Nov/2026:10:00:00 +0800] [Job 12] Unable to open file\n"
        "I [03/Nov/2026:10:00:01 +0800] [Job 123] other job\n"
        "E [03/Nov/2026:10:00:02 +0800] [Job 12] Job stopped due to filter error.\n"
        "E no job tag\n"
        "E [Job 1] [Job 12x]\n";
    const QString out = JobLogReader::extractJobLines(log, 12);
    EXPECT_TRUE(out.contains("Unable to open file"));
    EXPECT_TRUE(out.contains("filter error"));
    EXPECT_FALSE(out.contains("other job"));
    EXPECT_FALSE(out.contains("no job tag"));
    EXPECT_EQ(out.count('\n'), 2);
}

TEST(ut_JobLogReader, ExtractLimitsLineAndTotalSize)
{
    QByteArray log;
    const QByteArray longLine = "E [Job 5] " + QByteArray(5000, 'x') + "\n";
    for (int i = 0; i < 200; ++i)
        log += longLine;
    const QString out = JobLogReader::extractJobLines(log, 5);
    EXPECT_LE(out.toUtf8().size(), JobLogReader::kMaxOutputBytes);
    for (const QString &line : out.split('\n'))
        EXPECT_LE(line.size(), JobLogReader::kMaxLineLength);
    EXPECT_TRUE(JobLogReader::extractJobLines("", 5).isEmpty());
}

TEST(ut_JobLogReader, ReadJobLogStopsAtOlderJobAndFindsLateLines)
{
    QTemporaryFile file;
    ASSERT_TRUE(file.open());
    // A long prefix of older jobs, then the target job; chunks are tiny so
    // the reader has to walk back over many of them.
    for (int i = 0; i < 4000; ++i)
        file.write(QByteArray("E [Job 3] old line ") + QByteArray::number(i) + "\n");
    file.write("E [Job 4] first line\n");
    for (int i = 0; i < 4000; ++i)
        file.write("I [Job 9] unrelated newer job\n");
    file.write("E [Job 4] last line\n");
    file.flush();

    const QString out = JobLogReader::readJobLog(file.fileName(), 4);
    EXPECT_TRUE(out.contains("first line"));
    EXPECT_TRUE(out.contains("last line"));
    EXPECT_FALSE(out.contains("old line"));
    EXPECT_FALSE(out.contains("unrelated"));
}

TEST(ut_JobLogReader, ReadJobLogRespectsBudgetAndBadInput)
{
    QTemporaryFile file;
    ASSERT_TRUE(file.open());
    file.write("E [Job 4] early\n");
    file.write(QByteArray(200 * 1024, 'x') + "\n");
    file.flush();

    // The budget (one chunk) does not reach the line at the start of the file.
    EXPECT_FALSE(JobLogReader::readJobLog(file.fileName(), 4, 1).contains("early"));
    EXPECT_TRUE(JobLogReader::readJobLog(file.fileName(), 4).contains("early"));
    EXPECT_TRUE(JobLogReader::readJobLog(file.fileName(), 0).isEmpty());
    EXPECT_TRUE(JobLogReader::readJobLog("/nonexistent/error_log", 4).isEmpty());
}

TEST(ut_JobLogReader, UserNameLookup)
{
    EXPECT_EQ(JobLogReader::userNameOf(0), QString("root"));
    EXPECT_TRUE(JobLogReader::userNameOf(4000000000u).isEmpty());
}

TEST(ut_RepairJobs, KnownFixIds)
{
    EXPECT_TRUE(RepairJobs::isKnownFix("cups_service"));
    EXPECT_TRUE(RepairJobs::isKnownFix("canon_lbp2900"));
    EXPECT_TRUE(RepairJobs::isKnownFix("brother_dcp_mfc"));
    EXPECT_FALSE(RepairJobs::isKnownFix(""));
    EXPECT_FALSE(RepairJobs::isKnownFix("cups_service; reboot"));
    EXPECT_FALSE(RepairJobs::isKnownFix("../etc/passwd"));
    EXPECT_FALSE(RepairJobs::isKnownFix("pantum_p33xx"));
}

TEST(ut_RepairJobs, CanonLibrariesAreFixed)
{
    const QStringList packages = RepairJobs::canonI386Packages();
    EXPECT_EQ(packages, (QStringList{"libpopt0:i386", "libxml2:i386"}));
}

TEST(ut_RepairJobs, ForeignArchitectureDetection)
{
    EXPECT_TRUE(RepairJobs::hasForeignArchitecture("i386\n", "i386"));
    EXPECT_TRUE(RepairJobs::hasForeignArchitecture("arm64\ni386\n", "i386"));
    EXPECT_FALSE(RepairJobs::hasForeignArchitecture("", "i386"));
    EXPECT_FALSE(RepairJobs::hasForeignArchitecture("arm64\n", "i386"));
    EXPECT_FALSE(RepairJobs::hasForeignArchitecture("i3866\n", "i386"));
    EXPECT_EQ(RepairJobs::foreignArchitectureArgs(), (QStringList{"--print-foreign-architectures"}));
}

TEST(ut_RepairJobs, AptInstallArguments)
{
    const QStringList args = RepairJobs::aptInstallArgs(RepairJobs::canonI386Packages());
    EXPECT_EQ(args.first(), QString("-y"));
    EXPECT_TRUE(args.contains("install"));
    EXPECT_EQ(args.mid(args.size() - 2), RepairJobs::canonI386Packages());
}

TEST(ut_RepairJobs, PermissionStripping)
{
    const QFileDevice::Permissions loose = QFileDevice::ReadOwner | QFileDevice::WriteOwner
        | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::WriteOther | QFileDevice::ReadOther;
    const QFileDevice::Permissions fixed = RepairJobs::withoutGroupOtherWrite(loose);
    EXPECT_FALSE(fixed & QFileDevice::WriteGroup);
    EXPECT_FALSE(fixed & QFileDevice::WriteOther);
    EXPECT_TRUE(fixed & QFileDevice::WriteOwner);
    EXPECT_TRUE(fixed & QFileDevice::ReadGroup);
    EXPECT_TRUE(fixed & QFileDevice::ExeOwner);
}

TEST(ut_RepairJobs, RepairableFilterNames)
{
    EXPECT_TRUE(RepairJobs::isRepairableFilterName("brother_lpdwrapper_DCP7090DW"));
    EXPECT_TRUE(RepairJobs::isRepairableFilterName("brother_mfc7380_filter.pl"));
    EXPECT_FALSE(RepairJobs::isRepairableFilterName("rastertopwg"));
    EXPECT_FALSE(RepairJobs::isRepairableFilterName("brother_"));
    EXPECT_FALSE(RepairJobs::isRepairableFilterName("brother_a/b"));
    EXPECT_FALSE(RepairJobs::isRepairableFilterName("brother_a\n"));
}

TEST(ut_RepairJobs, UnknownFixFailsWithoutRunning)
{
    QObject owner;
    int code = -1;
    RepairJobs::run(&owner, "no_such_fix", [&code](int c) { code = c; });
    EXPECT_EQ(code, 1);
}

TEST(ut_JobLogReader, SubmissionTimeParsesLpstatDates)
{
    const qint64 am = JobLogReader::submissionTime(QString("Mon 03 Nov 2026 10:00:00 AM CST").split(' '));
    const qint64 pm = JobLogReader::submissionTime(QString("Mon 03 Nov 2026 10:00:00 PM CST").split(' '));
    const qint64 h24 = JobLogReader::submissionTime(QString("Mon 03 Nov 2026 22:00:00 CST").split(' '));
    EXPECT_GT(am, 0);
    EXPECT_EQ(pm - am, 12 * 3600);
    EXPECT_EQ(h24, pm);
    EXPECT_EQ(JobLogReader::submissionTime(QStringList{"Mon"}), 0);
    EXPECT_EQ(JobLogReader::submissionTime(QStringList()), 0);
}

TEST(ut_JobLogReader, ParseLpstatRecordsSubmissionTime)
{
    const auto jobs = JobLogReader::parseLpstatJobs(
        "Q-12   alice   1024   Mon 03 Nov 2026 10:00:00 AM CST\n", "Q");
    ASSERT_EQ(jobs.size(), 1);
    EXPECT_GT(jobs.at(0).submittedEpoch, 0);
}

TEST(ut_JobLogReader, NewestEntryKeepsTime)
{
    QList<JobLogReader::JobEntry> jobs;
    jobs.append({5, "alice", 100});
    jobs.append({9, "alice", 200});
    jobs.append({11, "bob", 300});
    const auto entry = JobLogReader::newestEntryOf(jobs, "alice");
    EXPECT_EQ(entry.id, 9);
    EXPECT_EQ(entry.submittedEpoch, 200);
    EXPECT_EQ(JobLogReader::newestEntryOf(jobs, "nobody").id, 0);
}

TEST(ut_JobLogReader, ReusedJobIdOlderLinesAreSkipped)
{
    const QByteArray log =
        "I [03/Nov/2026:08:00:00 +0000] [Job 7] old job of someone else\n"
        "I [03/Nov/2026:10:00:05 +0000] [Job 7] new job line\n"
        "I [03/Nov/2026:10:00:06 +0000] [Job 7] second line\n";
    // 2026-11-03 10:00:00 UTC
    const qint64 created = QDateTime(QDate(2026, 11, 3), QTime(10, 0, 0), Qt::UTC).toSecsSinceEpoch();
    const QString filtered = JobLogReader::extractJobLines(log, 7, created);
    EXPECT_FALSE(filtered.contains("someone else"));
    EXPECT_TRUE(filtered.contains("new job line"));
    EXPECT_TRUE(filtered.contains("second line"));
    // without a creation time nothing is dropped
    EXPECT_TRUE(JobLogReader::extractJobLines(log, 7).contains("someone else"));
}

TEST(ut_JobLogReader, LineTimezoneOffsetIsApplied)
{
    const QByteArray log = "I [03/Nov/2026:18:00:05 +0800] [Job 3] same instant as 10:00:05 UTC\n";
    const qint64 created = QDateTime(QDate(2026, 11, 3), QTime(10, 0, 0), Qt::UTC).toSecsSinceEpoch();
    EXPECT_TRUE(JobLogReader::extractJobLines(log, 3, created).contains("same instant"));
    // a job created an hour later than the line: the line is stale
    EXPECT_TRUE(JobLogReader::extractJobLines(log, 3, created + 3600).isEmpty());
}

TEST(ut_JobLogReader, LinesWithoutStampAreKept)
{
    const QByteArray log = "D [Job 4] no timestamp here\n";
    EXPECT_FALSE(JobLogReader::extractJobLines(log, 4, 4000000000LL).isEmpty());
}

TEST(ut_JobLogReader, SubmissionTimeAcceptsOtherLpstatFormats)
{
    const qint64 ref = JobLogReader::submissionTime(QString("Mon 02 Nov 2026 23:59:00 UTC").split(' '));
    ASSERT_GT(ref, 0);
    EXPECT_EQ(JobLogReader::submissionTime(QString("Mon Nov  2 23:59:00 2026").split(' ')), ref);
    EXPECT_EQ(JobLogReader::submissionTime(QString("2026-11-02T23:59:00").split(' ')), ref);
    EXPECT_EQ(JobLogReader::submissionTime(QString("2026-11-02 23:59:00").split(' ')), ref);
    EXPECT_EQ(JobLogReader::submissionTime(QString("02 Nov 2026 11:59:00 PM").split(' ')), ref);
    EXPECT_EQ(JobLogReader::submissionTime(QString("Mon 02 Nov 2026 11:59:00 PM UTC").split(' ')), ref);
}

TEST(ut_JobLogReader, SubmissionTimeIsZeroForUnreadableDates)
{
    EXPECT_EQ(JobLogReader::submissionTime(QString("Mon 02 Foo 2026 11:59:00 PM").split(' ')), 0);
    EXPECT_EQ(JobLogReader::submissionTime(QString("Mon 32 Nov 2026 11:59:00 PM").split(' ')), 0);
    EXPECT_EQ(JobLogReader::submissionTime(QString("Mon 02 Nov 2026 25:59:00").split(' ')), 0);
    EXPECT_EQ(JobLogReader::submissionTime(QString("Mon 02 Nov 2026 13:00:00 PM").split(' ')), 0);
    EXPECT_EQ(JobLogReader::submissionTime(QString("gestern abend").split(' ')), 0);
}

TEST(ut_JobLogReader, UnreadableDateKeepsOldBehaviourOfReturningTheLog)
{
    const auto jobs = JobLogReader::parseLpstatJobs("Q-12   alice   1024   garbled date here\n", "Q");
    ASSERT_EQ(jobs.size(), 1);
    EXPECT_EQ(jobs.at(0).submittedEpoch, 0);
    const QByteArray log = "I [01/Jan/2000:00:00:00 +0000] [Job 12] old but kept\n";
    EXPECT_FALSE(JobLogReader::extractJobLines(log, 12, jobs.at(0).submittedEpoch).isEmpty());
}

// ------------------------------------------------- readLatestJobLog

namespace {

class ut_JobLogReaderLatest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tools.isValid());
        ASSERT_TRUE(m_dir.isValid());
        m_user = JobLogReader::userNameOf(::geteuid());
        if (m_user.isEmpty())
            GTEST_SKIP() << "the current uid has no passwd entry";
        m_logPath = m_dir.filePath("error_log");
        JobLogReader::setErrorLogPath(m_logPath);
    }
    void TearDown() override { JobLogReader::setErrorLogPath(QString()); }

    void writeLog(const QByteArray &text)
    {
        QFile file(m_logPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(text);
    }

    QString lpstatJobs(const QString &extra = QString()) const
    {
        // the date column is deliberately unreadable: no time filtering
        return QString("printf '%s\\n' 'P1-3 %1 1024 unknown' 'P1-5 %1 2048 unknown' 'P1-6 nobody-else 10 unknown' '%2'")
            .arg(m_user, extra);
    }

    QString latest(const QString &printer = "P1")
    {
        QObject owner;
        const auto waited = waitForResult<QString>([&](std::function<void(QString)> cb) {
            JobLogReader::readLatestJobLog(&owner, ::geteuid(), printer, [cb](const QString &log) { cb(log); });
        });
        EXPECT_EQ(waited.calls, 1) << "done must be called exactly once";
        return waited.value.value_or(QString("<no answer>"));
    }

    FakeTools m_tools;
    QTemporaryDir m_dir;
    QString m_user;
    QString m_logPath;
};

}  // namespace

TEST_F(ut_JobLogReaderLatest, returnsTheLinesOfTheNewestJobOfTheCaller)
{
    m_tools.add("lpstat", lpstatJobs());
    writeLog("I [Job 3] old\nI [Job 5] first line\nI [Job 6] someone else\nI [Job 5] second line\nI no job tag\n");
    EXPECT_EQ(latest(), QString("I [Job 5] first line\nI [Job 5] second line\n"));
    EXPECT_EQ(m_tools.callsOf("lpstat"), (QList<QStringList>{{"-W", "all", "-o", "P1"}}));
}

TEST_F(ut_JobLogReaderLatest, callerWithoutJobsGetsNothing)
{
    m_tools.add("lpstat", "printf '%s\\n' 'P1-6 nobody-else 10 unknown'");
    writeLog("I [Job 6] someone else\n");
    EXPECT_TRUE(latest().isEmpty());
}

TEST_F(ut_JobLogReaderLatest, failingLpstatGetsNothing)
{
    m_tools.add("lpstat", lpstatJobs() + "; exit 1");
    writeLog("I [Job 5] line\n");
    EXPECT_TRUE(latest().isEmpty());
}

TEST_F(ut_JobLogReaderLatest, missingLpstatRunsNothing)
{
    writeLog("I [Job 5] line\n");
    EXPECT_TRUE(latest().isEmpty());
    EXPECT_TRUE(m_tools.calls().isEmpty());
}

TEST_F(ut_JobLogReaderLatest, missingLogFileGetsNothing)
{
    m_tools.add("lpstat", lpstatJobs());
    EXPECT_TRUE(latest().isEmpty());
}

TEST_F(ut_JobLogReaderLatest, otherPrintersJobsAreIgnored)
{
    m_tools.add("lpstat", lpstatJobs());
    writeLog("I [Job 5] line\n");
    EXPECT_TRUE(latest("P2").isEmpty());
}

TEST_F(ut_JobLogReaderLatest, unknownUidGetsNothingAndRunsNoCommand)
{
    m_tools.add("lpstat", lpstatJobs());
    QObject owner;
    const auto waited = waitForResult<QString>([&](std::function<void(QString)> cb) {
        JobLogReader::readLatestJobLog(&owner, 4000000000u, "P1", [cb](const QString &log) { cb(log); });
    });
    ASSERT_EQ(waited.calls, 1);
    EXPECT_TRUE(waited.value->isEmpty());
    EXPECT_TRUE(m_tools.calls().isEmpty());
}
