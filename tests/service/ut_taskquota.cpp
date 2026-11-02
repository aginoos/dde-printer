// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "taskquota.h"

namespace {

TaskQuota::Limits smallLimits()
{
    TaskQuota::Limits limits;
    limits.privilegedPerUid = 2;
    limits.logReadPerUid = 1;
    limits.pendingGlobal = 5;
    limits.runningGlobal = 2;
    limits.logReadGlobal = 2;
    return limits;
}

}  // namespace

TEST(ut_TaskQuota, perUidLimitDoesNotAffectOtherUsers)
{
    TaskQuota quota(smallLimits());
    TaskQuota::Ticket a1, a2, a3, b1;
    EXPECT_TRUE(quota.admit(1000, TaskQuota::Kind::Privileged, &a1));
    EXPECT_TRUE(quota.admit(1000, TaskQuota::Kind::Privileged, &a2));
    EXPECT_FALSE(quota.admit(1000, TaskQuota::Kind::Privileged, &a3));
    EXPECT_TRUE(quota.admit(1001, TaskQuota::Kind::Privileged, &b1));
    EXPECT_EQ(quota.inFlightOf(1000, TaskQuota::Kind::Privileged), 2);
}

TEST(ut_TaskQuota, pendingAuthorizationDoesNotUseRunningSlots)
{
    TaskQuota quota(smallLimits());
    TaskQuota::Ticket a1, a2, b1;
    ASSERT_TRUE(quota.admit(1000, TaskQuota::Kind::Privileged, &a1));
    ASSERT_TRUE(quota.admit(1000, TaskQuota::Kind::Privileged, &a2));
    EXPECT_EQ(quota.pendingGlobal(), 2);
    EXPECT_EQ(quota.runningGlobal(), 0);
    ASSERT_TRUE(quota.admit(1001, TaskQuota::Kind::Privileged, &b1));
    EXPECT_TRUE(quota.startRunning(&b1));
    EXPECT_EQ(quota.runningGlobal(), 1);
    EXPECT_EQ(quota.pendingGlobal(), 2);
}

TEST(ut_TaskQuota, runningSlotsAreGlobalAndCapped)
{
    TaskQuota quota(smallLimits());
    TaskQuota::Ticket t1, t2, t3;
    ASSERT_TRUE(quota.admit(1, TaskQuota::Kind::Privileged, &t1));
    ASSERT_TRUE(quota.admit(2, TaskQuota::Kind::Privileged, &t2));
    ASSERT_TRUE(quota.admit(3, TaskQuota::Kind::Privileged, &t3));
    EXPECT_TRUE(quota.startRunning(&t1));
    EXPECT_TRUE(quota.startRunning(&t2));
    EXPECT_FALSE(quota.startRunning(&t3));
    EXPECT_FALSE(t3.running);
    quota.release(&t1);
    EXPECT_TRUE(quota.startRunning(&t3));
}

TEST(ut_TaskQuota, globalPendingCap)
{
    TaskQuota quota(smallLimits());
    TaskQuota::Ticket tickets[6];
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(quota.admit(100 + i, TaskQuota::Kind::Privileged, &tickets[i]));
    EXPECT_FALSE(quota.admit(200, TaskQuota::Kind::Privileged, &tickets[5]));
}

TEST(ut_TaskQuota, releaseIsIdempotentAndFreesCounters)
{
    TaskQuota quota(smallLimits());
    TaskQuota::Ticket t;
    ASSERT_TRUE(quota.admit(5, TaskQuota::Kind::Privileged, &t));
    ASSERT_TRUE(quota.startRunning(&t));
    quota.release(&t);
    quota.release(&t);
    EXPECT_EQ(quota.runningGlobal(), 0);
    EXPECT_EQ(quota.pendingGlobal(), 0);
    EXPECT_EQ(quota.inFlightOf(5, TaskQuota::Kind::Privileged), 0);
}

TEST(ut_TaskQuota, releaseOfPendingTicketFreesPending)
{
    TaskQuota quota(smallLimits());
    TaskQuota::Ticket t;
    ASSERT_TRUE(quota.admit(5, TaskQuota::Kind::Privileged, &t));
    quota.release(&t);
    EXPECT_EQ(quota.pendingGlobal(), 0);
    EXPECT_FALSE(quota.startRunning(&t));
}

TEST(ut_TaskQuota, logReadsHaveTighterLimitsAndAreIndependent)
{
    TaskQuota quota(smallLimits());
    TaskQuota::Ticket l1, l2, l3, l4, p1;
    EXPECT_TRUE(quota.admit(1, TaskQuota::Kind::LogRead, &l1));
    EXPECT_FALSE(quota.admit(1, TaskQuota::Kind::LogRead, &l2));
    EXPECT_TRUE(quota.admit(2, TaskQuota::Kind::LogRead, &l3));
    EXPECT_FALSE(quota.admit(3, TaskQuota::Kind::LogRead, &l4));   // global cap of 2
    EXPECT_TRUE(quota.admit(1, TaskQuota::Kind::Privileged, &p1)); // not affected
    quota.release(&l1);
    EXPECT_TRUE(quota.admit(1, TaskQuota::Kind::LogRead, &l2));
    EXPECT_FALSE(quota.startRunning(&l2));
}
