// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IPPUSBDIAG_H
#define IPPUSBDIAG_H

#include "avahi_tool.h"

#include <QList>
#include <QString>

// Why a printer on a USB device uri cannot be reached directly: ipp-usb takes
// the USB interface over and exposes the printer as an IPP network printer.
namespace IppUsbDiag {

enum class Verdict {
    NotInvolved,   // ipp-usb is not installed or does not manage the device
    TakenOver,     // ipp-usb manages the device and is healthy: use the driverless queue
    Unhealthy,     // ipp-usb manages the device but reports a problem
};

struct Result
{
    Verdict verdict = Verdict::NotInvolved;
    QString status;  // status text reported by ipp-usb for Unhealthy
};

// Model part of a usb://Make/Model?serial=... or hp:/usb/Make_Model?... uri;
// empty for other uris.
QString modelFromDeviceUri(const QString &uri);

// Case-insensitive comparison tolerant to "series" suffixes and separators.
bool sameModel(const QString &uriModel, const QString &ippUsbModel);

// Pure decision on the given device list.
Result diagnose(const QString &deviceUri, const QList<AvahiTool::IppUsbDevice> &devices);

// Same with the devices reported by `ipp-usb status` (blocking).
Result diagnoseLocal(const QString &deviceUri);

}  // namespace IppUsbDiag

#endif  // IPPUSBDIAG_H
