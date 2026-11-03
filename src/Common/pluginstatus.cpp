// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pluginstatus.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFileInfo>

namespace PluginStatus {

const char kPluginService[] = "org.deepin.ddeprinter";

namespace {
const char kMetadataFile[] = "/usr/share/deepin-service-manager/user/dde-printer.json";
}

bool needsRelogin(bool metadataInstalled, bool serviceRegistered)
{
    return metadataInstalled && !serviceRegistered;
}

bool metadataInstalled()
{
    return QFileInfo::exists(QString::fromLatin1(kMetadataFile));
}

bool serviceRegistered()
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus)
        return false;
    const QDBusReply<bool> reply = bus->isServiceRegistered(QString::fromLatin1(kPluginService));
    return reply.isValid() && reply.value();
}

}   // namespace PluginStatus
