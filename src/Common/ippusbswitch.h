// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IPPUSBSWITCH_H
#define IPPUSBSWITCH_H

#include <QString>

/*
 * Adapts the device URI of a printer that ipp-usb publishes on a loopback port
 * (ipp://localhost:60000) to the way it is going to be added:
 *   - driverless: the port changes when ipp-usb restarts or several printers are
 *     plugged in, so the queue uses the dnssd:// address of the service;
 *   - with a vendor driver: ipp-usb has to release the printer (blacklist) and the
 *     classic usb:// address of the device is used.
 * Both calls block for seconds; in the GUI thread the event loop keeps running
 * with user input excluded.
 */
namespace IppUsbSwitch {

struct Result
{
    bool ok = false;
    QString uri;     // the new device URI when ok
    QString error;   // user-readable reason when not ok
};

Result driverlessUri(const QString &ippUsbUri);
Result usbBackendUri(const QString &model);

}   // namespace IppUsbSwitch

#endif   // IPPUSBSWITCH_H
