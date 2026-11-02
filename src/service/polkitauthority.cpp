// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "polkitauthority.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QMap>
#include <QMetaType>
#include <QVariant>

namespace {

const char kPolkitService[] = "org.freedesktop.PolicyKit1";
const char kPolkitPath[] = "/org/freedesktop/PolicyKit1/Authority";
const char kPolkitInterface[] = "org.freedesktop.PolicyKit1.Authority";

constexpr quint32 kAllowUserInteraction = 1;
constexpr int kCallTimeoutMs = 120000; // the user may take a while to type a password

// Polkit Subject: (sa{sv}) = ("system-bus-name", {"name": <busName>})
QDBusArgument makeSubject(const QString &busName)
{
    QDBusArgument subject;
    subject.beginStructure();
    subject << QStringLiteral("system-bus-name");
    subject.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QDBusVariant>());
    subject.beginMapEntry();
    subject << QStringLiteral("name") << QDBusVariant(busName);
    subject.endMapEntry();
    subject.endMap();
    subject.endStructure();
    return subject;
}

QDBusArgument makeEmptyDetails()
{
    QDBusArgument details;
    details.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QString>());
    details.endMap();
    return details;
}

// AuthorizationResult: (bba{ss})
AuthResult parseReply(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return AuthResult::Error;

    const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
    bool authorized = false;
    bool challenge = false;
    QMap<QString, QString> details;
    arg.beginStructure();
    arg >> authorized >> challenge >> details;
    arg.endStructure();
    return authorized ? AuthResult::Authorized : AuthResult::Denied;
}

}

namespace PolkitAuthority {

void check(const QDBusConnection &connection, const QString &busName,
           const QString &action, QObject *context, Callback done)
{
    if (busName.isEmpty()) {
        done(AuthResult::Denied);
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        kPolkitService, kPolkitPath, kPolkitInterface, QStringLiteral("CheckAuthorization"));
    call << QVariant::fromValue(makeSubject(busName)) << action
         << QVariant::fromValue(makeEmptyDetails()) << kAllowUserInteraction << QString();

    auto *watcher = new QDBusPendingCallWatcher(connection.asyncCall(call, kCallTimeoutMs), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
                     [watcher, done](QDBusPendingCallWatcher *) {
                         const AuthResult result = parseReply(watcher->reply());
                         watcher->deleteLater();
                         done(result);
                     });
}

}
