// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HELPERINTERFACE_H
#define HELPERINTERFACE_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QTimer>

#include "zcupsmonitor.h"

class HelperInterface : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", SERVICE_INTERFACE_NAME)
public:
    explicit HelperInterface(CupsMonitor *pCupsMonitor, QObject *parent = nullptr);
    ~HelperInterface();

    bool registerDBus();
    void unRegisterDBus();
signals:
    Q_SCRIPTABLE void signalJobStateChanged(int id, int state, const QString &message);
    Q_SCRIPTABLE void signalPrinterStateChanged(const QString &printer, int state, const QString &message);
    Q_SCRIPTABLE void signalPrinterDelete(const QString &printer);
    Q_SCRIPTABLE void signalPrinterAdd(const QString &printer);
    Q_SCRIPTABLE void deviceStatusChanged(const QString &defaultPrinterName, int status);
    void usbDeviceProcess();

public slots:
    //dbus接口
    Q_SCRIPTABLE bool isJobPurged(int id);
    Q_SCRIPTABLE QString getJobNotify(const QMap<QString, QVariant> &job);
    Q_SCRIPTABLE QString getStateString(int iState);
    Q_SCRIPTABLE void setTypeAndState(int status);

protected:
    void slotShowTrayIcon(bool bShow);
    void showJobsWindow();


private:
    CupsMonitor *m_pCupsMonitor;
    QSystemTrayIcon *m_pSystemTray;
    QTimer* m_timer;
};

#endif // HELPERINTERFACE_H
