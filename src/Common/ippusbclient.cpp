// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ippusbclient.h"

#include "common.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QEventLoop>
#include <QRegularExpression>
#include <QThread>

namespace IppUsbClient {

namespace {

const char kService[] = "com.deepin.printer.manager";
const char kPath[] = "/com/deepin/printer/manager";
// polkit may show a password dialog and wait for the user
const int kCallTimeoutMs = 120 * 1000;

// Result codes of IppUsbBlacklist::Result in the service
const int kCodeOk = 0;
const int kCodeNotListed = 2;
const int kCodeNotInstalled = 3;

bool isWellFormedId(const QString &vendorProduct)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[0-9a-fA-F]{4}:[0-9a-fA-F]{4}\\z"));
    return pattern.match(vendorProduct).hasMatch();
}

// The service is root-only by bus policy; verify the owner anyway so that a
// spoofed service can never answer for it.
bool isServiceOwnedByRoot()
{
    QDBusConnectionInterface *bus = QDBusConnection::systemBus().interface();
    if (!bus)
        return false;
    const QString name = QString::fromLatin1(kService);
    if (!bus->isServiceRegistered(name).value())
        bus->startService(name);
    const QDBusReply<uint> uid = bus->serviceUid(name);
    return uid.isValid() && uid.value() == 0;
}

QDBusMessage callService(const QString &method, const QString &vendorProduct)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                                                      QString::fromLatin1(kService), method);
    msg << vendorProduct;

    const bool inGuiThread = QCoreApplication::instance()
            && QThread::currentThread() == QCoreApplication::instance()->thread();
    if (!inGuiThread)
        return QDBusConnection::systemBus().call(msg, QDBus::Block, kCallTimeoutMs);

    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(msg, kCallTimeoutMs);
    QDBusPendingCallWatcher watcher(pending);
    QEventLoop loop;
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    if (!pending.isFinished())
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    return pending.reply();
}

Transport g_transport;

Outcome call(const QString &method, const QString &vendorProduct)
{
    if (!isWellFormedId(vendorProduct))
        return Outcome::InvalidArgument;
    const bool ownedByRoot = g_transport.isServiceOwnedByRoot ? g_transport.isServiceOwnedByRoot() : isServiceOwnedByRoot();
    if (!ownedByRoot)
        return Outcome::Unavailable;
    return outcomeFromReply(g_transport.callService ? g_transport.callService(method, vendorProduct)
                                                    : callService(method, vendorProduct));
}

Outcome outcomeFromError(const QDBusMessage &reply)
{
    const QString name = reply.errorName();
    qCWarning(COMMONMOUDLE) << "ipp-usb blacklist call failed:" << name << reply.errorMessage();
    if (name == QDBusError::errorString(QDBusError::AccessDenied))
        return Outcome::Denied;
    if (name == QDBusError::errorString(QDBusError::InvalidArgs))
        return Outcome::InvalidArgument;
    if (name == QDBusError::errorString(QDBusError::LimitsExceeded))
        return Outcome::Busy;
    if (name == QDBusError::errorString(QDBusError::NoReply) || name == QDBusError::errorString(QDBusError::Timeout))
        return Outcome::Timeout;
    if (name == QDBusError::errorString(QDBusError::ServiceUnknown))
        return Outcome::Unavailable;
    return Outcome::Failed;
}

}

void setTransport(const Transport &transport) { g_transport = transport; }
void resetTransport() { g_transport = Transport(); }

bool isSuccess(Outcome outcome)
{
    return outcome == Outcome::Ok;
}

Outcome outcomeFromReply(const QDBusMessage &reply)
{
    if (reply.type() == QDBusMessage::ErrorMessage)
        return outcomeFromError(reply);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 1)
        return Outcome::Failed;

    bool isInt = false;
    const int code = reply.arguments().first().toInt(&isInt);
    if (!isInt)
        return Outcome::Failed;
    switch (code) {
    case kCodeOk:
        return Outcome::Ok;
    case kCodeNotListed:
        return Outcome::NotListed;
    case kCodeNotInstalled:
        return Outcome::Unavailable;
    default:
        return Outcome::Failed;
    }
}

Outcome addToBlacklist(const QString &vendorProduct)
{
    return call(QStringLiteral("AddDeviceToIppusbBlacklist"), vendorProduct);
}

Outcome removeFromBlacklist(const QString &vendorProduct)
{
    return call(QStringLiteral("RemoveDeviceFromIppusbBlacklist"), vendorProduct);
}

}   // namespace IppUsbClient
