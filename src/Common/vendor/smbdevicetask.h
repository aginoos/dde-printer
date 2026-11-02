// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SMBDEVICETASK_H
#define SMBDEVICETASK_H

#include "zdevicemanager.h"

#include <QString>

// Lists the shared printers of a Windows (SMB) host through libsmbclient.
// The identity is asked once per run through signalSmbPassWord; the resulting device URIs never
// contain a user name or password (see SmbUri / SecretStore for how credentials are kept).
class RefreshDevicesSmbTask : public RefreshDevicesTask
{
    Q_OBJECT

public:
    explicit RefreshDevicesSmbTask(const QString &host, int id = TASK_RefreshNetDev, QObject *parent = nullptr);

    // true when the user dismissed the identity dialog (no devices, no error)
    bool isCancelled() const { return m_cancelled; }

signals:
    // Connect with Qt::BlockingQueuedConnection so the worker thread waits for the answer.
    // The receiver raises `ret` when the user confirms and fills in the identity; an empty user
    // means guest access. `remember` reports the "remember password" choice.
    void signalSmbPassWord(int &ret, QString host, QString &group, QString &user, QString &password, bool &remember);

protected:
    int doWork() override;

private:
    struct Identity
    {
        QString workgroup;
        QString user;
        QString password;
    };

    // 0 on success, otherwise the errno of the failed open
    int listShares(const Identity &identity);

    QString m_host;
    bool m_cancelled = false;
};

#endif  // SMBDEVICETASK_H
