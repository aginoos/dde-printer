// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>
#include <QDebug>

#include "cupsconnection.h"
#include "zjobmanager.h"

class ut_zjobmanager : public testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

    Connection m_cupsConnection;
};

void ut_zjobmanager::SetUp()
{
    try {
        int ret = m_cupsConnection.init(cupsServer(), ippPort(), cupsEncryption());
        ASSERT_EQ(ret, 0);
    } catch (const std::runtime_error &e) {
        qInfo() << e.what();
    }
}

void ut_zjobmanager::TearDown()
{

}

TEST_F(ut_zjobmanager, getJobs)
{
    map<int, map<string, string>> jobs;
    int ret = g_jobManager->getJobs(jobs, WHICH_JOB_ALL);
    if (m_cupsConnection.http) {
        EXPECT_EQ(ret, 0);
    } else {

        EXPECT_TRUE(jobs.size() == 0);
    }
}

TEST_F(ut_zjobmanager, getJobById)
{
    map<string, string> jobinfo;
    int ret = g_jobManager->getJobById(jobinfo, 80);
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret == 0 || ret == -2);
    } else {

        EXPECT_TRUE(jobinfo.size() == 0);
    }
}

TEST_F(ut_zjobmanager, cancelJob)
{
    int ret = g_jobManager->cancelJob(1);
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret <= 0);
    }


}

TEST_F(ut_zjobmanager, deleteJob)
{
    int ret = g_jobManager->deleteJob(0);
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret <= 0);
    } else {
        EXPECT_EQ(ret, -1);
    }

    ret = g_jobManager->deleteJob(1, "TestPrinter");
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret <= 0);
    } else {
        EXPECT_EQ(ret, -1);
    }
}

TEST_F(ut_zjobmanager, holdJob)
{
    int ret = g_jobManager->holdJob(0);
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret <= 0);
    }


}

TEST_F(ut_zjobmanager, holdjobs)
{
    int ret = g_jobManager->holdjobs("TestPrinter");
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret == -1 || ret == 1);
    }


}

TEST_F(ut_zjobmanager, releaseJob)
{
    int ret = g_jobManager->releaseJob(0);
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret <= 0);
    }


}

TEST_F(ut_zjobmanager, restartJob)
{
    int ret = g_jobManager->restartJob(0);
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret <= 0);
    } else {

    }
}

TEST_F(ut_zjobmanager, restartJob1)
{
    int ret = g_jobManager->restartJob(91);
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret <= 0);
    } else {

    }
}

TEST_F(ut_zjobmanager, moveJob)
{
    char destUri[64] = "ipp://TestPrinter/jobs/100";
    char srcUri[64] = "ipp://TestPrinter1/jobs/TestPrinter1";
    int ret = g_jobManager->moveJob(destUri, 1, nullptr);
    if (m_cupsConnection.http) {
        EXPECT_EQ(ret, -1);
    }

    ret = g_jobManager->moveJob(nullptr, -1, srcUri);
    if (m_cupsConnection.http) {
        EXPECT_EQ(ret, -1);
    }

    ret = g_jobManager->moveJob(nullptr, 1, srcUri);
    if (m_cupsConnection.http) {
        EXPECT_EQ(ret, -1);
    }
}

TEST_F(ut_zjobmanager, priorityJob)
{
    int iPriority = DEFAULT_Priority;
    int ret = g_jobManager->priorityJob(1, iPriority);
    if (m_cupsConnection.http) {
        EXPECT_EQ(ret, -1);
    }

    iPriority = -1;
    ret = g_jobManager->priorityJob(1, iPriority);
    if (m_cupsConnection.http) {
        EXPECT_EQ(ret, -1);
    }
}

TEST_F(ut_zjobmanager, printTestPage)
{
    char printer[16] = "TestPrinter";
    int id = 1;
    QString ret = g_jobManager->printTestPage(printer, id, nullptr);
    if (m_cupsConnection.http) {
        EXPECT_TRUE(ret.size());
    }

}

TEST_F(ut_zjobmanager, isCompletedState)
{
    bool ret = g_jobManager->isCompletedState(IPP_JSTATE_CANCELED);
    EXPECT_TRUE(ret);

    ret = g_jobManager->isCompletedState(IPP_JSTATE_STOPPED);
    EXPECT_FALSE(ret);
}

// ---------------------------------------------------------------------------
// JobManager against a scripted backend: no CUPS server, no D-Bus service.

#include <QTemporaryFile>

#include <algorithm>

#include <stdexcept>

namespace {

// Attribute values are stored with a one character type prefix.
std::string str(const char *text) { return std::string("s") + text; }
std::string num(int value) { return std::string("i") + std::to_string(value); }

class FakeBackend : public JobBackend
{
public:
    struct JobsRequest
    {
        std::string which;
        int myJobs, limit, firstJobId;
        vector<string> attrs;
    };

    FakeBackend() { m_previous = g_jobManager->setBackend(this); }
    ~FakeBackend() override { g_jobManager->setBackend(m_previous); }

    void getJobs(map<int, map<string, string>> &jobs, const char *which, int myJobs,
                 int limit, int firstJobId, const vector<string> *attrs) override
    {
        if (throwOnGetJobs)
            throw std::runtime_error("getJobs failed");
        requests.push_back({which, myJobs, limit, firstJobId, attrs ? *attrs : vector<string>()});
        jobs = scripted;
    }
    bool cancelJob(int jobId, int purge) override
    {
        calls << QString("cancel %1 purge=%2").arg(jobId).arg(purge);
        if (throwOnWrite)
            throw std::runtime_error("cancel failed");
        return reachable;
    }
    bool cancelAllJobs(const char *dest) override
    {
        calls << QString("cancelAll %1").arg(dest);
        if (throwOnWrite)
            throw std::runtime_error("cancelAll failed");
        return reachable;
    }
    void holdJob(int jobId) override { record(QString("hold %1").arg(jobId)); }
    void releaseJob(int jobId) override { record(QString("release %1").arg(jobId)); }
    void restartJob(int jobId) override { record(QString("restart %1").arg(jobId)); }
    void moveJob(const char *destUri, int jobId, const char *srcUri) override
    {
        record(QString("move %1 %2 %3").arg(destUri ? destUri : "(null)").arg(jobId).arg(srcUri ? srcUri : "(null)"));
    }
    void setJobPriority(int jobId, int priority) override
    {
        record(QString("priority %1 %2").arg(jobId).arg(priority));
    }
    void printTestPage(const char *dest, const char *file, const char *title,
                       const char *format, int &jobId) override
    {
        record(QStringList{"test", dest, file, title, format ? format : "(null)"}.join(' '));
        jobId = newJobId;
    }
    QString testPageFile() const override { return testFile; }
    std::function<bool(int)> purgedCheck() override
    {
        if (!purgeServiceUp)
            return nullptr;
        return [this](int id) { return purged.contains(id); };
    }

    map<int, map<string, string>> scripted;
    QList<int> purged;
    bool purgeServiceUp = true;
    bool throwOnGetJobs = false;
    bool throwOnWrite = false;
    bool reachable = true;
    int newJobId = 42;
    QString testFile;
    std::vector<JobsRequest> requests;
    QStringList calls;

private:
    void record(const QString &call)
    {
        calls << call;
        if (throwOnWrite)
            throw std::runtime_error("write failed");
    }

    JobBackend *m_previous = nullptr;
};

map<string, string> jobOn(const char *printer, int priority = 50)
{
    return {{JOB_ATTR_URI, str((std::string("ipp://localhost/printers/") + printer).c_str())},
            {JOB_ATTR_PRIORITY, num(priority)}};
}

}  // namespace

TEST(ut_JobManager, getJobsAsksForTheDocumentedAttributesAndMapsTheScope)
{
    FakeBackend backend;
    map<int, map<string, string>> jobs;

    ASSERT_EQ(g_jobManager->getJobs(jobs, WHICH_JOB_RUNING, 1), 0);
    ASSERT_EQ(g_jobManager->getJobs(jobs, WHICH_JOB_DONE, 0), 0);
    ASSERT_EQ(g_jobManager->getJobs(jobs, WHICH_JOB_ALL), 0);

    ASSERT_EQ(backend.requests.size(), 3u);
    EXPECT_EQ(backend.requests[0].which, std::string("not-completed"));
    EXPECT_EQ(backend.requests[0].myJobs, 1);
    EXPECT_EQ(backend.requests[1].which, std::string("completed"));
    EXPECT_EQ(backend.requests[1].myJobs, 0);
    EXPECT_EQ(backend.requests[2].which, std::string("all"));
    EXPECT_EQ(backend.requests[2].limit, 0);
    EXPECT_EQ(backend.requests[2].firstJobId, 0);
    EXPECT_EQ(backend.requests[2].attrs.size(), 13u);
    EXPECT_NE(std::find(backend.requests[2].attrs.begin(), backend.requests[2].attrs.end(), std::string(JOB_ATTR_STATE)),
              backend.requests[2].attrs.end());
}

TEST(ut_JobManager, getJobsReturnsTheJobsAndDropsPurgedOnes)
{
    FakeBackend backend;
    backend.scripted = {{1, jobOn("A")}, {2, jobOn("A")}, {3, jobOn("B")}};
    backend.purged = {2};

    map<int, map<string, string>> jobs;
    ASSERT_EQ(g_jobManager->getJobs(jobs), 0);
    ASSERT_EQ(jobs.size(), 2u);
    EXPECT_TRUE(jobs.count(1));
    EXPECT_FALSE(jobs.count(2));
    EXPECT_TRUE(jobs.count(3));
}

TEST(ut_JobManager, getJobsKeepsEverythingWhenThePurgeServiceIsAbsent)
{
    FakeBackend backend;
    backend.scripted = {{1, jobOn("A")}, {2, jobOn("A")}};
    backend.purged = {1, 2};
    backend.purgeServiceUp = false;

    map<int, map<string, string>> jobs;
    ASSERT_EQ(g_jobManager->getJobs(jobs), 0);
    EXPECT_EQ(jobs.size(), 2u);
}

TEST(ut_JobManager, getJobsReportsAFailingServer)
{
    FakeBackend backend;
    backend.throwOnGetJobs = true;
    map<int, map<string, string>> jobs;
    EXPECT_EQ(g_jobManager->getJobs(jobs), -1);
    EXPECT_TRUE(jobs.empty());
}

TEST(ut_JobManager, getJobByIdFindsExactlyThatJob)
{
    FakeBackend backend;
    backend.scripted = {{7, jobOn("A", 70)}, {8, jobOn("B", 80)}};

    map<string, string> job;
    ASSERT_EQ(g_jobManager->getJobById(job, 8), 0);
    EXPECT_EQ(job[JOB_ATTR_PRIORITY], num(80));

    ASSERT_EQ(backend.requests.size(), 1u);
    EXPECT_EQ(backend.requests[0].which, std::string("all"));
    EXPECT_EQ(backend.requests[0].myJobs, 1);        // own jobs by default
    EXPECT_EQ(backend.requests[0].limit, 1);
    EXPECT_EQ(backend.requests[0].firstJobId, 8);

    ASSERT_EQ(g_jobManager->getJobById(job, 7, 0), 0);
    EXPECT_EQ(backend.requests[1].myJobs, 0);
}

TEST(ut_JobManager, getJobByIdReportsMissingJobAndErrors)
{
    FakeBackend backend;
    backend.scripted = {{7, jobOn("A")}};
    map<string, string> job;
    EXPECT_EQ(g_jobManager->getJobById(job, 99), -2);
    EXPECT_TRUE(job.empty());

    backend.throwOnGetJobs = true;
    EXPECT_EQ(g_jobManager->getJobById(job, 7), -1);
}

TEST(ut_JobManager, cancelJobDoesNotPurge)
{
    FakeBackend backend;
    EXPECT_EQ(g_jobManager->cancelJob(5), 0);
    EXPECT_EQ(backend.calls, (QStringList{"cancel 5 purge=0"}));

    backend.throwOnWrite = true;
    EXPECT_EQ(g_jobManager->cancelJob(6), -1);
}

TEST(ut_JobManager, deleteJobPurgesOneJobOrAllJobsOfAPrinter)
{
    FakeBackend backend;
    EXPECT_EQ(g_jobManager->deleteJob(9), 0);
    EXPECT_EQ(g_jobManager->deleteJob(0, "Q1"), 0);
    EXPECT_EQ(backend.calls, (QStringList{"cancel 9 purge=1", "cancelAll Q1"}));
}

TEST(ut_JobManager, deleteJobFailsWithoutServerAndOnErrors)
{
    FakeBackend backend;
    backend.reachable = false;
    EXPECT_EQ(g_jobManager->deleteJob(9), -1);
    EXPECT_EQ(g_jobManager->deleteJob(0, "Q1"), -1);

    backend.reachable = true;
    backend.throwOnWrite = true;
    EXPECT_EQ(g_jobManager->deleteJob(9), -1);
    EXPECT_EQ(g_jobManager->deleteJob(0, "Q1"), -1);
}

TEST(ut_JobManager, holdReleaseRestartAndMoveForwardTheirArguments)
{
    FakeBackend backend;
    EXPECT_EQ(g_jobManager->holdJob(3), 0);
    EXPECT_EQ(g_jobManager->releaseJob(3), 0);
    EXPECT_EQ(g_jobManager->restartJob(4), 0);
    EXPECT_EQ(g_jobManager->moveJob("ipp://h/printers/B", 5, "ipp://h/printers/A"), 0);
    EXPECT_EQ(g_jobManager->moveJob("ipp://h/printers/B", 0, nullptr), 0);
    EXPECT_EQ(backend.calls, (QStringList{"hold 3", "release 3", "restart 4",
                                          "move ipp://h/printers/B 5 ipp://h/printers/A",
                                          "move ipp://h/printers/B 0 (null)"}));
}

TEST(ut_JobManager, holdReleaseRestartAndMoveReportErrors)
{
    FakeBackend backend;
    backend.throwOnWrite = true;
    EXPECT_EQ(g_jobManager->holdJob(3), -1);
    EXPECT_EQ(g_jobManager->releaseJob(3), -1);
    EXPECT_EQ(g_jobManager->restartJob(3), -1);
    EXPECT_EQ(g_jobManager->moveJob("ipp://h/printers/B", 3, nullptr), -1);
}

TEST(ut_JobManager, holdjobsHoldsOnlyTheJobsOfThatPrinter)
{
    FakeBackend backend;
    backend.scripted = {{1, jobOn("A")}, {2, jobOn("B")}, {3, jobOn("A")}};

    EXPECT_TRUE(g_jobManager->holdjobs("A"));
    EXPECT_EQ(backend.calls, (QStringList{"hold 1", "hold 3"}));
    ASSERT_EQ(backend.requests.size(), 1u);
    EXPECT_EQ(backend.requests[0].which, std::string("not-completed"));   // finished jobs are not held

    backend.calls.clear();
    EXPECT_TRUE(g_jobManager->holdjobs("Nobody"));
    EXPECT_TRUE(backend.calls.isEmpty());
}

TEST(ut_JobManager, holdjobsFailsWhenTheJobListCannotBeRead)
{
    FakeBackend backend;
    backend.throwOnGetJobs = true;
    EXPECT_EQ(g_jobManager->holdjobs("A"), -1);
    EXPECT_TRUE(backend.calls.isEmpty());
}

TEST(ut_JobManager, priorityJobWithAnExplicitValueSetsExactlyThat)
{
    FakeBackend backend;
    int priority = 30;
    EXPECT_EQ(g_jobManager->priorityJob(5, priority), 0);
    EXPECT_EQ(priority, 30);
    EXPECT_EQ(backend.calls, (QStringList{"priority 5 30"}));
}

TEST(ut_JobManager, priorityJobAutoPicksOneAboveTheHighestRunningJob)
{
    FakeBackend backend;
    backend.scripted = {{2, jobOn("A", 50)}, {3, jobOn("A", 70)}, {4, jobOn("A", 10)}};

    int priority = -1;
    EXPECT_EQ(g_jobManager->priorityJob(9, priority), 0);
    EXPECT_EQ(priority, 71);
    EXPECT_EQ(backend.calls, (QStringList{"priority 9 71"}));
}

TEST(ut_JobManager, priorityJobAutoStartsAtTheDefaultWhenNothingIsHigher)
{
    FakeBackend backend;
    backend.scripted = {{2, jobOn("A", 10)}};
    int priority = -1;
    EXPECT_EQ(g_jobManager->priorityJob(9, priority), 0);
    EXPECT_EQ(priority, DEFAULT_Priority);
}

TEST(ut_JobManager, priorityJobAutoLowersAJobThatAlreadyHoldsTheTopValue)
{
    FakeBackend backend;
    backend.scripted = {{2, jobOn("A", HIGHEST_Priority)}};
    int priority = -1;
    EXPECT_EQ(g_jobManager->priorityJob(9, priority), 0);
    EXPECT_EQ(priority, HIGHEST_Priority);
    // The old top job gives way by one so the new job can be on top.
    EXPECT_EQ(backend.calls, (QStringList{QString("priority 2 %1").arg(HIGHEST_Priority - 1),
                                          QString("priority 9 %1").arg(HIGHEST_Priority)}));
}

TEST(ut_JobManager, priorityJobReportsErrors)
{
    FakeBackend backend;
    int priority = -1;
    backend.throwOnGetJobs = true;
    EXPECT_EQ(g_jobManager->priorityJob(1, priority), -1);

    backend.throwOnGetJobs = false;
    backend.throwOnWrite = true;
    priority = 40;
    EXPECT_EQ(g_jobManager->priorityJob(1, priority), -1);
}

TEST(ut_JobManager, printTestPageNamesAMissingTestFile)
{
    FakeBackend backend;
    backend.testFile = "/nonexistent/dir/testprint";
    int jobId = -1;
    const QString error = g_jobManager->printTestPage("Q", jobId);
    EXPECT_TRUE(error.startsWith("/nonexistent/dir/testprint"));
    EXPECT_TRUE(error.contains("not found"));
    EXPECT_EQ(jobId, -1);
    EXPECT_TRUE(backend.calls.isEmpty());
}

TEST(ut_JobManager, printTestPageSubmitsTheFileWithTheTestTitle)
{
    QTemporaryFile file;
    ASSERT_TRUE(file.open());
    FakeBackend backend;
    backend.testFile = file.fileName();
    backend.newJobId = 77;

    int jobId = -1;
    EXPECT_TRUE(g_jobManager->printTestPage("Q", jobId, "application/postscript").isEmpty());
    EXPECT_EQ(jobId, 77);
    EXPECT_EQ(backend.calls, (QStringList{QString("test Q %1 Test page application/postscript").arg(file.fileName())}));

    EXPECT_TRUE(g_jobManager->printTestPage("Q", jobId).isEmpty());
    EXPECT_TRUE(backend.calls.last().endsWith("(null)"));
}

TEST(ut_JobManager, printTestPageReturnsTheErrorOfTheServer)
{
    QTemporaryFile file;
    ASSERT_TRUE(file.open());
    FakeBackend backend;
    backend.testFile = file.fileName();
    backend.throwOnWrite = true;
    int jobId = -1;
    EXPECT_EQ(g_jobManager->printTestPage("Q", jobId), QString("write failed"));
}

TEST(ut_JobManager, completedStatesAreCompletedAbortedAndCanceled)
{
    for (int state : {IPP_JSTATE_COMPLETED, IPP_JSTATE_ABORTED, IPP_JSTATE_CANCELED})
        EXPECT_TRUE(g_jobManager->isCompletedState(state)) << state;
    for (int state : {IPP_JSTATE_PENDING, IPP_JSTATE_HELD, IPP_JSTATE_PROCESSING, IPP_JSTATE_STOPPED})
        EXPECT_FALSE(g_jobManager->isCompletedState(state)) << state;
}

TEST(ut_JobManager, theInstalledBackendIsRestoredWhenAnotherOneLeaves)
{
    FakeBackend first;
    map<int, map<string, string>> jobs;
    {
        FakeBackend second;
        EXPECT_EQ(g_jobManager->getJobs(jobs), 0);
        EXPECT_EQ(second.requests.size(), 1u);
        EXPECT_TRUE(first.requests.empty());
    }
    EXPECT_EQ(g_jobManager->getJobs(jobs), 0);
    EXPECT_EQ(first.requests.size(), 1u);
}
