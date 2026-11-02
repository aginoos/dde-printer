// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IPPUSBPLANNER_H
#define IPPUSBPLANNER_H

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

#include "avahi_tool.h"
#include "ippusbclient.h"
#include "vendor/zdevicemanager.h"

/*
 * Decides how (and whether) a USB printer that was just plugged in is added
 * automatically. The decision is a small state machine over facts it asks a
 * PlannerEnv for, so every branch can be tested with a fake environment.
 *
 * Outcomes:
 *   Skip        nothing to do (already added, unusable identity, CUPS unreachable)
 *   Traditional add through the usb/hp backend and a vendor or generic driver
 *   IppUsb      add the driverless queue through a dnssd:// URI published by ipp-usb
 */
namespace IppUsbPlanner {

// Identity read from the USB descriptors. All strings are device supplied.
struct UsbPrinter
{
    quint16 vendorId = 0;
    quint16 productId = 0;
    QString manufacturer;
    QString product;
    QString serial;
    bool ippUsbCapable = false;   // has a 7/1/4 (IPP over USB) interface

    QString vendorProduct() const;
    QString model() const;
};

// Device the CUPS usb/hp backends found.
struct BackendDevice
{
    QString uri;
    QString deviceClass;
    QString info;
    QString makeAndModel;
    QString deviceId;
    QString location;
};

class PlannerEnv
{
public:
    virtual ~PlannerEnv() = default;
    // Device URIs of all existing queues; nullopt when CUPS cannot be asked.
    virtual std::optional<QStringList> queueUris() = 0;
    // Devices of the usb and hp backends.
    virtual QList<BackendDevice> backendDevices() = 0;
    virtual bool ippUsbInstalled() = 0;
    virtual QList<AvahiTool::IppUsbDevice> ippUsbDevices() = 0;
    virtual QList<AvahiTool::ServiceRecord> ippUsbServices() = 0;
    virtual IppUsbClient::Outcome blacklist(const QString &vendorProduct) = 0;
    virtual void pause(int milliseconds) = 0;
};

struct Options
{
    int listingAttempts = 10;   // waits for `ipp-usb status` to show the device
    int serviceAttempts = 6;    // waits for the Avahi publication
    int backendAttempts = 12;   // waits for the usb backend after a blacklist
    int pauseMs = 1000;
};

struct Plan
{
    enum Kind { Skip, Traditional, IppUsb };
    Kind kind = Skip;
    TDeviceInfo device;   // valid unless Skip
    QString reason;       // for the log
};

Plan plan(const UsbPrinter &printer, PlannerEnv &env, const Options &options = Options());

}   // namespace IppUsbPlanner

#endif   // IPPUSBPLANNER_H
