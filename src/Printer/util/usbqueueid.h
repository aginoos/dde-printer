// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef USBQUEUEID_H
#define USBQUEUEID_H

#include <QList>
#include <QString>

// Finds the USB vendor:product of the printer behind a usb:// or hp:/usb/ queue,
// so the printer can leave the ipp-usb blacklist when its queue is deleted.
// The lookups are pure functions over data read from sysfs; the one function
// with side effects, releaseFromBlacklist(), never reports an error to the
// caller: a failed release must not block the deletion of the queue.
namespace UsbQueueId {

// One USB device as sysfs describes it; all strings come from the device.
struct SysfsDevice
{
    QString vendorProduct;   // "04b8:0e1a"
    QString manufacturer;
    QString product;
    QString serial;
};

// Devices below `root` (default /sys/bus/usb/devices); entries without valid ids are skipped.
QList<SysfsDevice> readSysfsDevices(const QString &root = QStringLiteral("/sys/bus/usb/devices"));

// "serial" query value of a backend URI, empty when absent.
QString serialFromUri(const QString &deviceUri);

// vendor:product of the device the URI names. The serial decides when the URI has one and the
// devices agree on it; otherwise the model must identify exactly one id. Empty when unknown,
// ambiguous, or the URI is not a usb backend URI.
QString vendorProductForUri(const QString &deviceUri, const QList<SysfsDevice> &devices);

// True when the quirks file content holds the entry written for this vendor:product.
bool quirksListDevice(const QString &quirksContent, const QString &vendorProduct);

// Takes the printer behind `deviceUri` off the ipp-usb blacklist when it is on it.
// Returns true when an entry was removed. Blocking (see IppUsbClient).
bool releaseFromBlacklist(const QString &deviceUri);

}   // namespace UsbQueueId

#endif   // USBQUEUEID_H
