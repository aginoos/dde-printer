// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "troubleshootservice.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>

namespace {

const char kService[] = "com.deepin.printer.manager";
const char kPath[] = "/com/deepin/printer/manager";

}  // namespace

namespace TroubleshootService {

Outcome outcomeForError(QDBusError::ErrorType type)
{
    return type == QDBusError::AccessDenied ? Outcome::Denied : Outcome::Unavailable;
}

Outcome outcomeForCode(int code)
{
    return code == 0 ? Outcome::Success : Outcome::Failed;
}

LogResult readJobErrorLog(const QString &printerName)
{
    LogResult result;
    QDBusInterface iface(kService, kPath, kService, QDBusConnection::systemBus());
    if (!iface.isValid())
        return result;

    iface.setTimeout(kLogTimeoutMs);
    const QDBusReply<QString> reply = iface.call("ReadJobErrorLog", printerName);
    if (!reply.isValid())
        return result;

    result.ok = true;
    result.text = reply.value();
    return result;
}

void runRepair(const QString &fixId, QObject *context, std::function<void(Outcome)> done)
{
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kService, "RunRepair");
    message << fixId;

    const QDBusPendingCall call =
        QDBusConnection::systemBus().asyncCall(message, kRepairTimeoutMs);
    auto *watcher = new QDBusPendingCallWatcher(call, context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
                     [watcher, done](QDBusPendingCallWatcher *) {
        const QDBusPendingReply<int> reply = *watcher;
        done(reply.isError() ? outcomeForError(reply.error().type()) : outcomeForCode(reply.value()));
        watcher->deleteLater();
    });
}

}  // namespace TroubleshootService
