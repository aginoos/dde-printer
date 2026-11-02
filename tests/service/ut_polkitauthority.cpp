// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QEventLoop>
#include <QObject>
#include <QTimer>
#include <QVariant>

// Included to reach parseReply() in the unnamed namespace.
#include "polkitauthority.cpp"

namespace {

// A connection object that was never connected to any bus.
QDBusConnection unconnectedBus()
{
    return QDBusConnection(QStringLiteral("dde-printer-ut-unconnected"));
}

}  // namespace

// ------------------------------------------------------------- parseReply

TEST(ut_PolkitAuthority, errorReplyIsAnError)
{
    const QDBusMessage reply = QDBusMessage::createError(
        QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown"), QStringLiteral("no polkit"));
    EXPECT_EQ(parseReply(reply), AuthResult::Error);
}

TEST(ut_PolkitAuthority, invalidMessageIsAnError)
{
    EXPECT_EQ(parseReply(QDBusMessage()), AuthResult::Error);
}

TEST(ut_PolkitAuthority, methodCallInPlaceOfReplyIsAnError)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.example"), QStringLiteral("/"), QStringLiteral("org.example"), QStringLiteral("M"));
    EXPECT_EQ(parseReply(call), AuthResult::Error);
}

TEST(ut_PolkitAuthority, replyWithoutBodyIsAnError)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.example"), QStringLiteral("/"), QStringLiteral("org.example"), QStringLiteral("M"));
    EXPECT_EQ(parseReply(call.createReply()), AuthResult::Error);
}

TEST(ut_PolkitAuthority, unparseableBodyIsNeverAuthorized)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.example"), QStringLiteral("/"), QStringLiteral("org.example"), QStringLiteral("M"));
    // anything that is not a well-formed (bba{ss}) structure must fail closed
    EXPECT_NE(parseReply(call.createReply(QVariant(QStringLiteral("authorized")))), AuthResult::Authorized);
    EXPECT_NE(parseReply(call.createReply(QVariant(true))), AuthResult::Authorized);
    EXPECT_NE(parseReply(call.createReply(QVariant(1))), AuthResult::Authorized);
}

// ----------------------------------------------------------------- check

TEST(ut_PolkitAuthority, emptyBusNameIsDeniedImmediately)
{
    QObject context;
    int calls = 0;
    AuthResult seen = AuthResult::Authorized;
    PolkitAuthority::check(unconnectedBus(), QString(), "com.deepin.printer.manager.canon-add", &context,
                           [&](AuthResult result) {
                               seen = result;
                               ++calls;
                           });
    EXPECT_EQ(calls, 1);   // synchronous: no event loop needed
    EXPECT_EQ(seen, AuthResult::Denied);
}

TEST(ut_PolkitAuthority, unreachablePolkitIsAnErrorNotAGrant)
{
    QObject context;
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);

    int calls = 0;
    AuthResult seen = AuthResult::Authorized;
    PolkitAuthority::check(unconnectedBus(), ":1.42", "com.deepin.printer.manager.canon-add", &context,
                           [&](AuthResult result) {
                               seen = result;
                               ++calls;
                               loop.quit();
                           });
    EXPECT_EQ(calls, 0);   // the answer is always delivered asynchronously
    guard.start(5000);
    loop.exec();

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen, AuthResult::Error);
}
