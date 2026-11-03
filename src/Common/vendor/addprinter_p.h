// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ADDPRINTER_P_H
#define ADDPRINTER_P_H

#include "addprinter.h"
#include "zdevicemanager.h"

#include <QProcess>
#include <QDBusMessage>

class PrinterServerInterface;

typedef struct tagPackageInfo {
    QString toString() { return packageName + ": " + packageVer; }

    QString packageName;
    QString packageVer;
} TPackageInfo;

class QDBusPendingCallWatcher;

// Installs driver packages through lastore (D-Bus) or, on systems without it,
// through pkexec + apt-get. All waiting is event driven; nothing blocks the UI.
class InstallInterface : public QObject
{
    Q_OBJECT

public:
    explicit InstallInterface(QObject *parent = nullptr);

    void setPackages(const QList<TPackageInfo> &packages);
    // allowRefresh: the package index may be refreshed once if a needed
    // package version is missing from it.
    void startInstallPackages(bool allowRefresh = true);

    QString getErrorString();

    virtual void stop();

signals:
    void signalStatus(int);

protected slots:
    void propertyChanged(const QDBusMessage &msg);
    void updateSourceChanged(const QDBusMessage &msg);

protected:
    QList<TPackageInfo> m_packages;
    QStringList m_installPackages;
    bool m_bQuit;
    QString m_jobPath;
    QString m_updatePath;
    QString m_strType;
    QString m_strStatus;
    QString m_strUpdateType;
    QString m_strUpdateStatus;
    QString m_strErr;

private slots:
    void onUpdateCallFinished(QDBusPendingCallWatcher *watcher);
    void onInstallCallFinished(QDBusPendingCallWatcher *watcher);
    void onAptFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    bool collectPackagesToInstall();
    bool hasUnavailablePackage(QString &missing) const;
    bool detectDowngrade();
    void requestSourceRefresh();
    void submitInstall();
    void startAptInstall();
    bool retryOnLock(const QString &text);
    void scheduleSubmit(int delayMs);
    void failWith(const QString &error);
    void disconnectJob();
    void disconnectUpdate();
    QString versionOf(const QString &package) const;

    QString m_service;           // lastore bus name, empty when not available
    QString m_strJobDesc;
    int m_lockRetries = 0;
    int m_timeoutRetries = 0;
    int m_disconnectRetries = 0;
    QProcess *m_aptProc = nullptr;
};

class InstallDriver : public InstallInterface
{
    Q_OBJECT

public:
    InstallDriver(const QMap<QString, QVariant> &solution, QObject *parent = nullptr);

    void doWork();

    void stop();

protected slots:
    void slotServerDone(int iCode, const QByteArray &result);

private:
    QMap<QString, QVariant> m_solution;
};

class AddCanonCAPTPrinter : public AddPrinterTask
{
    Q_OBJECT

public:
    AddCanonCAPTPrinter(const TDeviceInfo &printer, const QMap<QString, QVariant> &solution, const QString &uri, QObject *parent = nullptr);

    void stop();

protected:
    int fixDriverDepends();
    int addPrinter();

protected slots:
    void slotProcessFinished(int iCode, QProcess::ExitStatus exitStatus);

private:
    QProcess m_proc;
};

class DefaultAddPrinter : public AddPrinterTask
{
    Q_OBJECT

public:
    DefaultAddPrinter(const TDeviceInfo &printer, const QMap<QString, QVariant> &solution, const QString &uri, QObject *parent = nullptr);

    void stop();

protected:
    int addPrinter();

    // Seams for the cleanup of a queue that lpadmin left behind after failing.
    virtual QStringList existingQueueNames();
    virtual bool removeQueue(const QString &name);
    void rememberExistingQueue();
    void rollbackFailedQueue();

protected slots:
    void slotProcessFinished(int iCode, QProcess::ExitStatus exitStatus);

private:
    QProcess m_proc;
    bool m_queueExistedBefore = false;
};

class FixHplipBackend : public QObject
{
    Q_OBJECT

public:
    FixHplipBackend(QObject *parent = nullptr);

    int startFixed();
    QString getMatchHplipUri(const QString &strUri);

    void stop();

    QString getErrorString();

signals:
    void signalStatus(int);

protected slots:
    void slotDeviceStatus(int id, int status);
    void slotInstallStatus(int status);

private:
    RefreshDevicesByBackendTask *m_deviceTask;
    TBackendSchemes m_hplipBackend;

    InstallInterface *m_installer;

    QString m_strError;
};

#endif //ADDPRINTER_P_H
