// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef USBPLANNERENV_H
#define USBPLANNERENV_H

#include "ippusbplanner.h"

// The real environment of the planner: CUPS, ipp-usb, Avahi and the manager service.
// Every call blocks, so use it from a worker thread only.
class LivePlannerEnv : public IppUsbPlanner::PlannerEnv
{
public:
    std::optional<QStringList> queueUris() override;
    QList<IppUsbPlanner::BackendDevice> backendDevices() override;
    bool ippUsbInstalled() override;
    QList<AvahiTool::IppUsbDevice> ippUsbDevices() override;
    QList<AvahiTool::ServiceRecord> ippUsbServices() override;
    IppUsbClient::Outcome blacklist(const QString &vendorProduct) override;
    void pause(int milliseconds) override;
};

#endif   // USBPLANNERENV_H
