// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usbplannerenv.h"

#include "common.h"
#include "cupsattrnames.h"
#include "cupsconnectionfactory.h"
#include "qtconvert.h"

#include <QStandardPaths>
#include <QThread>

#include <cups/cups.h>

#include <exception>
#include <map>
#include <string>
#include <vector>

namespace {

QString attribute(const std::map<std::string, std::string> &properties, const char *key)
{
    const auto it = properties.find(key);
    return it == properties.end() ? QString() : attrValueToQString(it->second);
}

}

std::optional<QStringList> LivePlannerEnv::queueUris()
{
    try {
        const auto connection = CupsConnectionFactory::createConnectionBySettings();
        if (!connection)
            return std::nullopt;

        QStringList uris;
        for (const auto &printer : connection->getPrinters()) {
            const QString uri = attribute(printer.second, CUPS_DEV_URI);
            if (!uri.isEmpty())
                uris << uri;
        }
        return uris;
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "cannot list the printer queues:" << QString::fromUtf8(ex.what());
        return std::nullopt;
    }
}

QList<IppUsbPlanner::BackendDevice> LivePlannerEnv::backendDevices()
{
    QList<IppUsbPlanner::BackendDevice> devices;
    try {
        const auto connection = CupsConnectionFactory::createConnectionBySettings();
        if (!connection)
            return devices;

        const std::vector<std::string> included{"usb", "hp"};
        const std::vector<std::string> excluded;
        const auto found = connection->getDevices(&excluded, &included, 0, CUPS_TIMEOUT_DEFAULT);
        for (const auto &entry : found) {
            IppUsbPlanner::BackendDevice device;
            device.uri = QString::fromStdString(entry.first);
            device.deviceClass = attribute(entry.second, CUPS_DEV_CLASS);
            device.info = attribute(entry.second, CUPS_DEV_INFO);
            device.makeAndModel = attribute(entry.second, CUPS_DEV_MAKE_MODE);
            device.deviceId = attribute(entry.second, CUPS_DEV_ID);
            device.location = attribute(entry.second, CUPS_DEV_LOCATION);
            devices.append(device);
        }
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "cannot list the usb backend devices:" << QString::fromUtf8(ex.what());
    }
    return devices;
}

bool LivePlannerEnv::ippUsbInstalled()
{
    return !QStandardPaths::findExecutable(QStringLiteral("ipp-usb"),
            {QStringLiteral("/usr/sbin"), QStringLiteral("/sbin"), QStringLiteral("/usr/bin")}).isEmpty();
}

QList<AvahiTool::IppUsbDevice> LivePlannerEnv::ippUsbDevices()
{
    return AvahiTool::ippUsbDevices();
}

QList<AvahiTool::ServiceRecord> LivePlannerEnv::ippUsbServices()
{
    return AvahiTool::localIppUsbServices();
}

IppUsbClient::Outcome LivePlannerEnv::blacklist(const QString &vendorProduct)
{
    const IppUsbClient::Outcome outcome = IppUsbClient::addToBlacklist(vendorProduct);
    if (!IppUsbClient::isSuccess(outcome))
        qCWarning(COMMONMOUDLE) << "could not blacklist" << vendorProduct << "in ipp-usb, outcome" << static_cast<int>(outcome);
    return outcome;
}

void LivePlannerEnv::pause(int milliseconds)
{
    QThread::msleep(static_cast<unsigned long>(milliseconds));
}
