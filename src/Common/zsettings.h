// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ZSETTINGS_H
#define ZSETTINGS_H

#include <QByteArray>
#include <QSettings>

class zSettings : public QSettings
{
public:
    static zSettings *getInstance();

    const QString getClientVersion();
    const QString getClientCode();
    const QString getHostName();
    unsigned short getHostPort();
    const QString getLogRules();
    const QString getOSVersion();
    const QString getDriverPlatformUrl();
    // `override` (the DRIVER_PLATFORM_TEST_URL value) wins only when allowed.
    static QString resolveDriverPlatformUrl(const QByteArray &override, bool allowOverride);
    const QString getSystemArch(); 
    int getSubscriptionId();
    void setSubscriptionId(int id);

    int getSequenceNumber();
    void setSequenceNumber(int number);

    const QString getCupsServerHost();
    int getCupsServerPort();
    int getCupsServerEncryption();
    QString getSysInfo();
    QString majorVersion();
    QString minorVersion();

protected:
    zSettings(const QString &fileName);
    ~zSettings();
};

#define g_Settings zSettings::getInstance()

#endif // ZSETTINGS_H
