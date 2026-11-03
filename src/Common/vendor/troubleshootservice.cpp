// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "troubleshootservice.h"

#include "../repaircodes.h"

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
    switch (code) {
    case RepairCode::Ok:
        return Outcome::Success;
    case RepairCode::ArchitectureFailed:
        return Outcome::ArchitectureFailed;
    case RepairCode::UpdateFailed:
        return Outcome::UpdateFailed;
    case RepairCode::InstallFailed:
        return Outcome::InstallFailed;
    case RepairCode::PackageManagerBusy:
        return Outcome::PackageManagerBusy;
    default:
        return Outcome::Failed;
    }
}

LogResult readJobErrorLog(const QString &printerName)
{
    LogResult result;
    // Not a QDBusInterface: it checks that the name has an owner first, and the service is
    // started by D-Bus on demand (and exits when idle), so it would be reported as unavailable.
    QDBusMessage request = QDBusMessage::createMethodCall(kService, kPath, kService, "ReadJobErrorLog");
    request << printerName;
    const QDBusReply<QString> reply = QDBusConnection::systemBus().call(request, QDBus::Block, kLogTimeoutMs);
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
