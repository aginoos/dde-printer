// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QByteArray>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QThread>

#include <atomic>

#include <DConfig>

// MLogger keeps its DConfig in a private member; the test (built with
// -fno-access-control) emits its valueChanged signal to reach the watcher.
#include "reviselogger.h"
#include "ztaskinterface.h"

namespace {

// Task with a scripted outcome. The statuses are collected from the worker
// thread, so they are guarded.
class ScriptedTask : public TaskInterface
{
public:
    explicit ScriptedTask(int id = TASK_TroubleShoot) : TaskInterface(id)
    {
        connect(this, &TaskInterface::signalStatus, this, [this](int task, int status) {
            QMutexLocker lock(&m_mutex);
            m_statuses.append({task, status});
        }, Qt::DirectConnection);
    }
    ~ScriptedTask() override { stop(); }

    QList<QPair<int, int>> statuses()
    {
        QMutexLocker lock(&m_mutex);
        return m_statuses;
    }

    int result = 0;          // returned by doWork
    int errorToSet = 0;      // written to m_errCode by doWork
    bool quitInside = false; // doWork asks to stop, as stop() would
    bool spin = false;       // doWork runs until stopped
    std::atomic<int> works{0};

protected:
    int doWork() override
    {
        ++works;
        if (errorToSet)
            m_errCode = errorToSet;
        if (quitInside)
            m_bQuit = true;
        while (spin && !m_bQuit)
            QThread::msleep(2);
        return result;
    }

private:
    QMutex m_mutex;
    QList<QPair<int, int>> m_statuses;
};

}  // namespace

TEST(ut_TaskInterface, newTaskHasNoError)
{
    ScriptedTask task;
    EXPECT_EQ(task.getErrCode(), 0);
    EXPECT_TRUE(task.getErrorString().isEmpty());
    EXPECT_FALSE(task.isRunning());
}

TEST(ut_TaskInterface, successReportsRunningThenSuccess)
{
    ScriptedTask task(TASK_RefreshNetDev);
    task.start();
    ASSERT_TRUE(task.wait(5000));

    EXPECT_EQ(task.works.load(), 1);
    EXPECT_EQ(task.statuses(), (QList<QPair<int, int>>{{TASK_RefreshNetDev, TStat_Running},
                                                       {TASK_RefreshNetDev, TStat_Suc}}));
    EXPECT_EQ(task.getErrCode(), 0);
}

TEST(ut_TaskInterface, failureReportsFailAndKeepsTheReturnValueAsErrorCode)
{
    ScriptedTask task;
    task.result = -3;
    task.start();
    ASSERT_TRUE(task.wait(5000));

    EXPECT_EQ(task.statuses().last(), (QPair<int, int>{TASK_TroubleShoot, TStat_Fail}));
    EXPECT_EQ(task.getErrCode(), -3);
}

TEST(ut_TaskInterface, anErrorCodeSetByTheWorkIsNotOverwritten)
{
    ScriptedTask task;
    task.errorToSet = 7;
    task.result = -1;
    task.start();
    ASSERT_TRUE(task.wait(5000));

    EXPECT_EQ(task.getErrCode(), 7);
    EXPECT_EQ(task.statuses().last().second, static_cast<int>(TStat_Fail));
}

TEST(ut_TaskInterface, quitDuringTheWorkSuppressesTheFinalStatus)
{
    ScriptedTask task;
    task.quitInside = true;
    task.result = 0;
    task.start();
    ASSERT_TRUE(task.wait(5000));

    // Only the start is reported: a stopped task never claims success or failure.
    EXPECT_EQ(task.statuses(), (QList<QPair<int, int>>{{TASK_TroubleShoot, TStat_Running}}));
    EXPECT_EQ(task.getErrCode(), 0);
}

TEST(ut_TaskInterface, stopEndsARunningTaskAndDropsItsConnections)
{
    ScriptedTask task;
    task.spin = true;
    task.start();
    for (int i = 0; i < 500 && task.works.load() == 0; ++i)
        QThread::msleep(2);
    ASSERT_EQ(task.works.load(), 1);
    ASSERT_TRUE(task.isRunning());

    task.stop();

    EXPECT_FALSE(task.isRunning());
    EXPECT_TRUE(task.m_bQuit);
    EXPECT_EQ(task.statuses().size(), 1);   // no Suc/Fail after a stop
}

TEST(ut_TaskInterface, stopOnAnIdleTaskOnlyMarksIt)
{
    ScriptedTask task;
    task.stop();
    EXPECT_TRUE(task.m_bQuit);
    EXPECT_FALSE(task.isRunning());
    EXPECT_EQ(task.works.load(), 0);
}

TEST(ut_TaskInterface, everyTaskTypeHasANameForTheLog)
{
    // The destructor and run() index a text table by task id.
    for (int id = TASK_NULL; id < TASK_MAX; ++id) {
        ScriptedTask task(id);
        task.start();
        ASSERT_TRUE(task.wait(5000)) << id;
        EXPECT_EQ(task.statuses().first().first, id);
    }
}

// ------------------------------------------------------------------ MLogger

namespace {

// Keeps QT_LOGGING_RULES of the test process out of the MLogger tests.
class LoggingRulesEnv
{
public:
    explicit LoggingRulesEnv(const QByteArray &value)
    {
        m_hadValue = qEnvironmentVariableIsSet("QT_LOGGING_RULES");
        m_old = qgetenv("QT_LOGGING_RULES");
        if (value.isNull())
            qunsetenv("QT_LOGGING_RULES");
        else
            qputenv("QT_LOGGING_RULES", value);
    }
    ~LoggingRulesEnv()
    {
        if (m_hadValue)
            qputenv("QT_LOGGING_RULES", m_old);
        else
            qunsetenv("QT_LOGGING_RULES");
        QLoggingCategory::setFilterRules(QString());
    }

private:
    bool m_hadValue;
    QByteArray m_old;
};

}  // namespace

TEST(ut_MLogger, rulesFromTheEnvironmentAreTakenOverAndTheVariableIsCleared)
{
    LoggingRulesEnv env("org.deepin.test.a=false;org.deepin.test.b=true");
    MLogger logger;

    EXPECT_EQ(logger.rules(), QString("org.deepin.test.a=false\norg.deepin.test.b=true"));
    EXPECT_FALSE(qEnvironmentVariableIsSet("QT_LOGGING_RULES"));
}

TEST(ut_MLogger, withoutEnvironmentRulesTheRulesStayEmpty)
{
    LoggingRulesEnv env(QByteArray{});
    MLogger logger;
    // No dconfig is installed for the test, so nothing is appended either.
    EXPECT_TRUE(logger.rules().isEmpty());
}

TEST(ut_MLogger, setRulesTurnsSemicolonsIntoLinesAndAppliesThem)
{
    LoggingRulesEnv env(QByteArray{});
    MLogger logger;

    logger.setRules("org.deepin.test.hidden.debug=false;org.deepin.test.hidden.warning=true");
    EXPECT_EQ(logger.rules(), QString("org.deepin.test.hidden.debug=false\norg.deepin.test.hidden.warning=true"));

    const QLoggingCategory category("org.deepin.test.hidden");
    EXPECT_FALSE(category.isDebugEnabled());
    EXPECT_TRUE(category.isWarningEnabled());
}

TEST(ut_MLogger, aChangedLogRulesKeyReplacesTheRules)
{
    LoggingRulesEnv env("org.deepin.test.x=false");
    MLogger logger;
    ASSERT_EQ(logger.rules(), QString("org.deepin.test.x=false"));

    // The key is re-read from dconfig, which has no value in the test: the
    // rules become empty. Other keys leave the rules alone.
    Q_EMIT logger.m_config->valueChanged(QStringLiteral("other_key"));
    EXPECT_EQ(logger.rules(), QString("org.deepin.test.x=false"));
    Q_EMIT logger.m_config->valueChanged(QStringLiteral("log_rules"));
    EXPECT_TRUE(logger.rules().isEmpty());
}
