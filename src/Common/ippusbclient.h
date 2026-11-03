// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IPPUSBCLIENT_H
#define IPPUSBCLIENT_H

#include <QDBusMessage>
#include <functional>
#include <QString>

/*
 * Client of the privileged ipp-usb blacklist methods of com.deepin.printer.manager.
 * A call that does not end in a clean "0" is a failure (never fail open); callers
 * fall back to the classic usb backend instead of aborting.
 */
namespace IppUsbClient {

enum class Outcome {
    Ok,
    InvalidArgument,   // vendor:product rejected before or by the service
    Denied,            // polkit refused or was dismissed
    Busy,
    Unavailable,       // service missing, not owned by root, or ipp-usb not installed
    Timeout,
    NotListed,         // ipp-usb does not list the device
    Failed,            // any other failure, including an unexpected reply
};

bool isSuccess(Outcome outcome);

// Translate the reply of the service (pure; exercised by the unit tests).
Outcome outcomeFromReply(const QDBusMessage &reply);

// Blocking. In the GUI thread the event loop keeps running (user input excluded)
// while polkit may ask for a password.
Outcome addToBlacklist(const QString &vendorProduct);
Outcome removeFromBlacklist(const QString &vendorProduct);

// Seam for tests: the two bus interactions behind the calls above. An empty member keeps the real
// system-bus behaviour (owner check, then the blocking/async method call).
struct Transport
{
    std::function<bool()> isServiceOwnedByRoot;
    std::function<QDBusMessage(const QString &method, const QString &vendorProduct)> callService;
};
void setTransport(const Transport &transport);
void resetTransport();

}   // namespace IppUsbClient

#endif   // IPPUSBCLIENT_H
