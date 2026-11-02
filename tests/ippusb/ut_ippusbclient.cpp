// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QDBusError>
#include <QDBusMessage>

#include "ippusbclient.h"

using namespace IppUsbClient;

namespace {

QDBusMessage request()
{
    return QDBusMessage::createMethodCall("com.deepin.printer.manager", "/com/deepin/printer/manager",
                                          "com.deepin.printer.manager", "AddDeviceToIppusbBlacklist");
}

QDBusMessage replyWith(const QVariant &value)
{
    return request().createReply(value);
}

QDBusMessage errorReply(QDBusError::ErrorType type)
{
    return request().createErrorReply(type, "x");
}

}

TEST(ut_IppUsbClient, successOnlyForCleanZero)
{
    EXPECT_EQ(outcomeFromReply(replyWith(0)), Outcome::Ok);
    EXPECT_TRUE(isSuccess(Outcome::Ok));
    EXPECT_FALSE(isSuccess(Outcome::Failed));
}

TEST(ut_IppUsbClient, serviceCodes)
{
    EXPECT_EQ(outcomeFromReply(replyWith(1)), Outcome::Failed);
    EXPECT_EQ(outcomeFromReply(replyWith(2)), Outcome::NotListed);
    EXPECT_EQ(outcomeFromReply(replyWith(3)), Outcome::Unavailable);
    EXPECT_EQ(outcomeFromReply(replyWith(4)), Outcome::Failed);
    EXPECT_EQ(outcomeFromReply(replyWith(5)), Outcome::Failed);   // written but not effective
    EXPECT_EQ(outcomeFromReply(replyWith(-7)), Outcome::Failed);
}

TEST(ut_IppUsbClient, malformedRepliesFailClosed)
{
    EXPECT_EQ(outcomeFromReply(replyWith("zero")), Outcome::Failed);
    EXPECT_EQ(outcomeFromReply(QDBusMessage()), Outcome::Failed);
    EXPECT_EQ(outcomeFromReply(request().createReply(QVariantList{0, 0})), Outcome::Failed);
}

TEST(ut_IppUsbClient, errorsAreMapped)
{
    EXPECT_EQ(outcomeFromReply(errorReply(QDBusError::AccessDenied)), Outcome::Denied);
    EXPECT_EQ(outcomeFromReply(errorReply(QDBusError::InvalidArgs)), Outcome::InvalidArgument);
    EXPECT_EQ(outcomeFromReply(errorReply(QDBusError::LimitsExceeded)), Outcome::Busy);
    EXPECT_EQ(outcomeFromReply(errorReply(QDBusError::NoReply)), Outcome::Timeout);
    EXPECT_EQ(outcomeFromReply(errorReply(QDBusError::ServiceUnknown)), Outcome::Unavailable);
    EXPECT_EQ(outcomeFromReply(errorReply(QDBusError::Other)), Outcome::Failed);
}

TEST(ut_IppUsbClient, malformedIdIsRejectedWithoutTouchingTheBus)
{
    EXPECT_EQ(addToBlacklist("04b8"), Outcome::InvalidArgument);
    EXPECT_EQ(addToBlacklist("04b8:12345"), Outcome::InvalidArgument);
    EXPECT_EQ(addToBlacklist("zzzz:0000"), Outcome::InvalidArgument);
    EXPECT_EQ(removeFromBlacklist("04b8:0e1a;rm"), Outcome::InvalidArgument);
    EXPECT_EQ(addToBlacklist(""), Outcome::InvalidArgument);
}

TEST(ut_IppUsbClient, malformedIdsAreRefusedBeforeAnyBusCall)
{
    for (const char *bad : {"", "03f0", "03f0:", "03f0:02Z1", "03f0:02a1 ", "03f0:02a1\n", "../etc:passwd", "03f0:02a1:00"}) {
        EXPECT_EQ(addToBlacklist(bad), Outcome::InvalidArgument) << bad;
        EXPECT_EQ(removeFromBlacklist(bad), Outcome::InvalidArgument) << bad;
    }
}

TEST(ut_IppUsbClient, otherErrorsAreFailures)
{
    EXPECT_EQ(outcomeFromReply(errorReply(QDBusError::InvalidArgs)), Outcome::InvalidArgument);
    EXPECT_FALSE(isSuccess(outcomeFromReply(errorReply(QDBusError::NoReply))));
    EXPECT_FALSE(isSuccess(outcomeFromReply(errorReply(QDBusError::ServiceUnknown))));
}
