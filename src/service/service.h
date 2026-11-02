// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QTimer>

#include <QDBusContext>
#include <QDBusMessage>
#include <QVariantMap>

#include <functional>

#define SERVICE_MANAGER_INTERFACE_NAME "com.deepin.printer.manager"
#define SERVICE_MANAGER_INTERFACE_PATH "/com/deepin/printer/manager"

// System bus service running as root. Every method asks polkit about the
// calling bus name before doing anything privileged; replies are delayed so
// neither the polkit dialog nor the helper tools block the event loop.
class Service : public QObject, public QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", SERVICE_MANAGER_INTERFACE_NAME)
public:
    explicit Service(QObject *parent = Q_NULLPTR);

public slots:
    void LaunchAutoStart(const QString &filePath);
    int CanonPrinterInstall(const QStringList &args);
    int CanonPrinterRemove(const QStringList &args);
    // Boolean cupsd.conf switches only; see cupsconfig.h. Returns 0.
    int SetServerSettings(const QVariantMap &settings);
    QString ReadJobErrorLog(const QString &printerName);
    int RunRepair(const QString &fixId);

private:
    using GrantedHandler = std::function<void(const QDBusMessage &request)>;

    // Starts an asynchronous polkit check for the current caller. Returns
    // false (after sending an error reply) when the service is overloaded.
    bool authorizeThen(const QString &action, GrantedHandler onGranted);
    void completeWith(const QDBusMessage &reply);
    void beginTask();
    void endTask();

private:
    int m_activeTasks = 0;
    bool m_canonBusy = false;
    bool m_settingsBusy = false;
    QTimer m_idleTimer;
};
