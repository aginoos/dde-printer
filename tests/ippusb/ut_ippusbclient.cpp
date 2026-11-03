// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QDBusError>
#include <QDBusMessage>
#include <QStringList>

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

namespace {

struct TransportScope
{
    bool owned = true;
    int ownerChecks = 0;
    QStringList methods;
    QStringList ids;
    QDBusMessage reply = replyWith(0);

    TransportScope()
    {
        Transport transport;
        transport.isServiceOwnedByRoot = [this]() { ++ownerChecks; return owned; };
        transport.callService = [this](const QString &method, const QString &id) {
            methods << method;
            ids << id;
            return reply;
        };
        setTransport(transport);
    }
    ~TransportScope() { resetTransport(); }
};

}  // namespace

TEST(ut_IppUsbClient, addCallsServiceMethodWithTheId)
{
    TransportScope t;
    EXPECT_EQ(addToBlacklist("04b8:0e1a"), Outcome::Ok);
    EXPECT_EQ(t.methods, QStringList{"AddDeviceToIppusbBlacklist"});
    EXPECT_EQ(t.ids, QStringList{"04b8:0e1a"});
}

TEST(ut_IppUsbClient, removeCallsServiceMethod)
{
    TransportScope t;
    t.reply = replyWith(2);
    EXPECT_EQ(removeFromBlacklist("04B8:0E1A"), Outcome::NotListed);
    EXPECT_EQ(t.methods, QStringList{"RemoveDeviceFromIppusbBlacklist"});
}

TEST(ut_IppUsbClient, serviceNotOwnedByRootIsUnavailableAndNeverCalled)
{
    TransportScope t;
    t.owned = false;
    EXPECT_EQ(addToBlacklist("04b8:0e1a"), Outcome::Unavailable);
    EXPECT_TRUE(t.methods.isEmpty());
}

TEST(ut_IppUsbClient, malformedIdSkipsOwnerCheckAndCall)
{
    TransportScope t;
    EXPECT_EQ(addToBlacklist("bad"), Outcome::InvalidArgument);
    EXPECT_EQ(t.ownerChecks, 0);
    EXPECT_TRUE(t.methods.isEmpty());
}

TEST(ut_IppUsbClient, errorRepliesFromTheServiceAreMapped)
{
    TransportScope t;
    t.reply = errorReply(QDBusError::AccessDenied);
    EXPECT_EQ(addToBlacklist("04b8:0e1a"), Outcome::Denied);
    t.reply = replyWith(3);
    EXPECT_EQ(addToBlacklist("04b8:0e1a"), Outcome::Unavailable);   // ipp-usb not installed
    t.reply = replyWith(99);
    EXPECT_EQ(addToBlacklist("04b8:0e1a"), Outcome::Failed);
}

TEST(ut_IppUsbClient, resetTransportRestoresMalformedIdHandling)
{
    {
        TransportScope t;
    }
    EXPECT_EQ(addToBlacklist("nope"), Outcome::InvalidArgument);
}
