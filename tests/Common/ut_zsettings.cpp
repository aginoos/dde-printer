// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QFileInfo>

#include "zsettings.h"

class ut_zsettings : public testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

private:
    QString m_configFile;
    bool isConfigFileExist = false;
};

void ut_zsettings::SetUp()
{
    QString strHome = getenv("HOME");
    m_configFile = strHome + "/.config/dde-printer.ini";
    QFileInfo fileInfo(m_configFile);
    if (fileInfo.exists()) {
        isConfigFileExist = true;
    } else {
        QSettings settings(m_configFile, QSettings::IniFormat);
        settings.beginGroup("[General]");
        settings.setValue("SequenceNumber", 1);
        settings.setValue("SubscriptionId", 1);
        settings.endGroup();
    }
}

void ut_zsettings::TearDown()
{
    if (!isConfigFileExist) {
        QFile::remove(m_configFile);
    }
}

TEST_F(ut_zsettings, getClientVersion)
{
    QString ret = g_Settings->getClientVersion();
    EXPECT_STREQ(ret.toStdString().c_str(), "1.2.0");
}

TEST_F(ut_zsettings, getClientCode)
{
    QString ret = g_Settings->getClientCode();
    EXPECT_STREQ(ret.toStdString().c_str(), "godfather");
}

TEST_F(ut_zsettings, getHostName)
{
    QString ret = g_Settings->getHostName();
    EXPECT_STREQ(ret.toStdString().c_str(), "printer.deepin.com");
}

TEST_F(ut_zsettings, getHostPort)
{
    unsigned short ret = g_Settings->getHostPort();
    EXPECT_EQ(ret, 80);
}

TEST_F(ut_zsettings, getLogRules)
{
    QString ret = g_Settings->getLogRules();
    EXPECT_STREQ(ret.toStdString().c_str(), "*.debug=false");
}

TEST_F(ut_zsettings, getOSVersion)
{
    QString ret = g_Settings->getOSVersion();
    // EXPECT_TRUE(ret.toStdString().c_str() == "" || ret.toStdString().size() > 0);
}

TEST_F(ut_zsettings, getSubscriptionId)
{
    int ret = g_Settings->getSubscriptionId();
    EXPECT_NE(ret, -2);
}

TEST_F(ut_zsettings, setSubscriptionId)
{
    g_Settings->setSubscriptionId(6);
    int ret = g_Settings->getSubscriptionId();
    EXPECT_EQ(ret, 6);
}

TEST_F(ut_zsettings, getSequenceNumber)
{
    int ret = g_Settings->getSequenceNumber();
    EXPECT_NE(ret, -1);
}

TEST_F(ut_zsettings, setSequenceNumber)
{
    g_Settings->setSequenceNumber(6);
    int ret = g_Settings->getSequenceNumber();
    EXPECT_EQ(ret, 6);
}

TEST_F(ut_zsettings, getCupsServerHost)
{
    QString ret = g_Settings->getCupsServerHost();
    EXPECT_TRUE(ret.toStdString().size() > 0);
}

TEST_F(ut_zsettings, getCupsServerPort)
{
    int ret = g_Settings->getCupsServerPort();
    EXPECT_EQ(ret, 631);
}

TEST_F(ut_zsettings, getCupsServerEncryption)
{
    int ret = g_Settings->getCupsServerEncryption();
    EXPECT_EQ(ret, 0);
}

TEST(ut_zsettingsDriverUrl, environmentOverrideIsIgnoredWhenNotAllowed)
{
    const QString configured = "https://drivers.example.invalid/search";
    EXPECT_EQ(zSettings::resolveDriverPlatformUrl("http://127.0.0.1:8080/x", false, configured), configured);
    EXPECT_EQ(zSettings::resolveDriverPlatformUrl("https://evil.example/x", false, QString()), QString());
}

TEST(ut_zsettingsDriverUrl, environmentOverrideAppliesOnlyWhenAllowedAndValid)
{
    const QString configured = "https://drivers.example.invalid/search";
    EXPECT_EQ(zSettings::resolveDriverPlatformUrl("http://127.0.0.1:8080/x", true, configured), QString("http://127.0.0.1:8080/x"));
    EXPECT_EQ(zSettings::resolveDriverPlatformUrl("", true, configured), configured);
    // an override that does not pass the validation falls back to the configured address
    EXPECT_EQ(zSettings::resolveDriverPlatformUrl("http://evil.example/x", true, configured), configured);
}

TEST(ut_zsettingsDriverUrl, defaultIsNoService)
{
    EXPECT_EQ(zSettings::resolveDriverPlatformUrl(QByteArray(), false, QString()), QString());
    EXPECT_EQ(zSettings::resolveDriverPlatformUrl(QByteArray(), false, "not a url"), QString());
}
