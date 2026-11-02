// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef USBTHREAD_H
#define USBTHREAD_H

#include <QThread>
#include <QMutex>
#include <QSemaphore>

#include "zdevicemanager.h"
#include "ippusbplanner.h"

#include <libusb-1.0/libusb.h>


class USBThread : public QThread
{
    Q_OBJECT
public:
    USBThread(QObject *parent = nullptr);
    ~USBThread() override;
    void getUsbDevice();
    void processArrivedUSBDevice();
    // Start a scan of the plugged-in printers; a request that arrives while a scan runs queues one more.
    void scan();

private:
    libusb_device *m_currentUSBDevice;
    QList<libusb_device *> m_usbDeviceList;
    QMutex m_mutex;
    QSemaphore m_workDone;
    bool m_scanRunning = false;
    bool m_rescanRequested = false;
    TDeviceInfo m_deviceInfo;

    QMap<uint, QString> m_pendingNotificationsMap;
    QString m_configingPrinterName;
    IppUsbPlanner::Plan::Kind m_planKind = IppUsbPlanner::Plan::Traditional;

private:
    void getDriver();

    void nextConfiguration();
    void planInBackground(const IppUsbPlanner::UsbPrinter &printer);
    void applyPlan(const IppUsbPlanner::Plan &plan);

private slots:
    bool addArrivedUSBPrinter();
    void notificationActionInvoked(uint id, const QString &msg);
    void addingJobFinished(int status);
signals:
    void newUSBDeviceArrived();
    void startGetDriver();
    void workFinished();
    /*通知前端当前正在配置的打印机状态变化*/
    void deviceStatusChanged(const QString &defaultPrinterName, int status);

protected:
   void run() override;
};

#endif // USBTHREAD_H
