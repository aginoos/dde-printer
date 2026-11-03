// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ZTROUBLESHOOT_ENV_H
#define ZTROUBLESHOOT_ENV_H

#include "cupserrormap.h"
#include "ippusbdiag.h"
#include "modelfixmap.h"
#include "troubleshootservice.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <string>
#include <vector>

// Everything the troubleshooting checks ask of the outside world: the CUPS
// server, the file system locations of printer data, name resolution, TCP
// connections, ping, package queries and the root service. The defaults do
// what the checks always did; tests replace single members.
struct TroubleShootEnv
{
    // Whether a connection to the configured CUPS server can be made.
    std::function<bool()> cupsReachable;
    // Path of the PPD file of a queue.
    std::function<QString(const QString &printer)> printerPpd;
    // Packages the driver of a PPD file depends on.
    std::function<QStringList(const QString &ppd)> driverDepends;
    std::function<bool(const QString &package)> packageExists;
    // Device uri of a queue.
    std::function<QString(const QString &printer)> printerUri;
    // Empty when the host resolves, otherwise the message for the user.
    std::function<QString(const QString &host)> resolveHost;
    // Empty when a TCP connection was made, otherwise the error text.
    std::function<QString(const QString &host, quint16 port, int timeoutMs)> tcpProbe;
    // Uris of the hp / usb devices the backends report.
    std::function<QStringList()> directDevices;
    std::function<IppUsbDiag::Result(const QString &deviceUri)> ippUsb;
    std::function<bool(const QString &host)> ping;
    // IPP attributes of a queue; may throw std::exception, an empty map when
    // there is no server.
    std::function<std::map<std::string, std::string>(const QString &printer, const std::vector<std::string> &requested)> printerAttributes;
    std::function<TroubleshootService::LogResult(const QString &printer)> readErrorLog;
    std::function<QList<CupsErrorEntry>()> errorTable;
    std::function<QList<ModelEntry>()> modelTable;
};

// The environment used by the checks; initialised with the real behaviour.
TroubleShootEnv &troubleShootEnv();
// A fresh copy of the real behaviour, for tests that want to restore it.
TroubleShootEnv defaultTroubleShootEnv();

#endif  // ZTROUBLESHOOT_ENV_H
