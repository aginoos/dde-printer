// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>
#include <QVariant>

#include "driverplatformconfig.h"

using DriverPlatformConfig::interpret;
using DriverPlatformConfig::normalize;

TEST(ut_DriverPlatformConfig, emptyAndBlankMeanNoService)
{
    EXPECT_EQ(normalize(""), QString());
    EXPECT_EQ(normalize("   "), QString());
    EXPECT_EQ(normalize("\t\n"), QString());
}

TEST(ut_DriverPlatformConfig, httpsAddressesAreAccepted)
{
    EXPECT_EQ(normalize("https://drivers.example.org/api/v1/search"), QString("https://drivers.example.org/api/v1/search"));
    EXPECT_EQ(normalize("  https://drivers.example.org:8443/x  "), QString("https://drivers.example.org:8443/x"));   // trimmed
    EXPECT_EQ(normalize("HTTPS://Drivers.Example.org/x"), QString("HTTPS://Drivers.Example.org/x"));
    EXPECT_EQ(normalize("https://drivers.example.org"), QString("https://drivers.example.org"));
}

TEST(ut_DriverPlatformConfig, plainHttpOnlyForLoopbackHosts)
{
    EXPECT_EQ(normalize("http://127.0.0.1:8080/search"), QString("http://127.0.0.1:8080/search"));
    EXPECT_EQ(normalize("http://localhost/search"), QString("http://localhost/search"));
    EXPECT_EQ(normalize("http://[::1]:9000/search"), QString("http://[::1]:9000/search"));
    EXPECT_EQ(normalize("http://drivers.example.org/search"), QString());
    EXPECT_EQ(normalize("http://192.168.1.10/search"), QString());
    EXPECT_EQ(normalize("http://127.0.0.1.evil.example/search"), QString());   // looks like a loopback prefix
}

TEST(ut_DriverPlatformConfig, otherSchemesAreRejected)
{
    EXPECT_EQ(normalize("ftp://drivers.example.org/x"), QString());
    EXPECT_EQ(normalize("file:///etc/passwd"), QString());
    EXPECT_EQ(normalize("javascript:alert(1)"), QString());
    EXPECT_EQ(normalize("drivers.example.org/x"), QString());   // no scheme
    EXPECT_EQ(normalize("//drivers.example.org/x"), QString());
}

TEST(ut_DriverPlatformConfig, userInfoQueryAndFragmentAreRejected)
{
    EXPECT_EQ(normalize("https://user:secret@drivers.example.org/x"), QString());
    EXPECT_EQ(normalize("https://user@drivers.example.org/x"), QString());
    EXPECT_EQ(normalize("https://drivers.example.org/x?token=1"), QString());   // the service appends its own query
    EXPECT_EQ(normalize("https://drivers.example.org/x?"), QString());
    EXPECT_EQ(normalize("https://drivers.example.org/x#frag"), QString());
}

TEST(ut_DriverPlatformConfig, controlCharactersSpacesAndEmptyHostsAreRejected)
{
    EXPECT_EQ(normalize("https://drivers.example.org/a b"), QString());
    EXPECT_EQ(normalize(QString("https://drivers.example.org/a\nb")), QString());
    EXPECT_EQ(normalize(QString("https://drivers.example.org/a") + QChar(0x7f)), QString());
    EXPECT_EQ(normalize("https:///path"), QString());
    EXPECT_EQ(normalize("https://"), QString());
}

TEST(ut_DriverPlatformConfig, overlongValuesAreRejected)
{
    const QString longPath = QString("/") + QString(600, QLatin1Char('a'));
    EXPECT_EQ(normalize("https://drivers.example.org" + longPath), QString());
    const QString ok = QString("https://drivers.example.org/") + QString(400, QLatin1Char('a'));
    EXPECT_EQ(normalize(ok), ok);
}

TEST(ut_DriverPlatformConfig, interpretOnlyTakesStrings)
{
    EXPECT_EQ(interpret(QVariant()), QString());
    EXPECT_EQ(interpret(QVariant(true)), QString());
    EXPECT_EQ(interpret(QVariant(42)), QString());
    EXPECT_EQ(interpret(QVariant(QStringList{"https://drivers.example.org/x"})), QString());
    EXPECT_EQ(interpret(QVariant(QString("https://drivers.example.org/x"))), QString("https://drivers.example.org/x"));
    EXPECT_EQ(interpret(QVariant(QString("junk"))), QString());
}

TEST(ut_DriverPlatformConfig, keyMatchesTheMetadataFile)
{
    EXPECT_STREQ(DriverPlatformConfig::kKey, "driver_platform_url");
}
