// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cupsconfig.h"

#include <gtest/gtest.h>

namespace {

QVariantMap one(const char *key, const QVariant &value)
{
    return {{QString::fromLatin1(key), value}};
}

}

TEST(ut_CupsConfig, acceptsEachWhitelistedSwitch)
{
    for (const char *key : {CupsConfig::kShare, CupsConfig::kRemoteAdmin, CupsConfig::kRemoteAny,
                            CupsConfig::kUserCancelAny, CupsConfig::kDebugLogging}) {
        QString error;
        const auto parsed = CupsConfig::parse(one(key, true), &error);
        ASSERT_TRUE(parsed.has_value()) << key << ": " << error.toStdString();
        ASSERT_EQ(parsed->size(), 1);
        EXPECT_EQ(parsed->first().key, QByteArray(key));
        EXPECT_TRUE(parsed->first().enabled);
    }
}

TEST(ut_CupsConfig, keepsFalseValues)
{
    const auto parsed = CupsConfig::parse(one(CupsConfig::kShare, false));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->first().enabled);
}

TEST(ut_CupsConfig, rejectsEmptyMap)
{
    QString error;
    EXPECT_FALSE(CupsConfig::parse({}, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(ut_CupsConfig, rejectsUnknownKeys)
{
    for (const char *key : {"_foo", "", "Listen", "_share_printers ", "_SHARE_PRINTERS",
                            "_share_printers\nPermitRoot yes", "LogLevel debug2"}) {
        QString error;
        EXPECT_FALSE(CupsConfig::parse(one(key, true), &error).has_value()) << key;
        EXPECT_FALSE(error.isEmpty()) << key;
    }
}

TEST(ut_CupsConfig, rejectsNonBooleanValues)
{
    for (const QVariant &value : {QVariant(QStringLiteral("1")), QVariant(1), QVariant(QStringLiteral("true")),
                                  QVariant(QStringLiteral("1\nPermitRoot yes")), QVariant()}) {
        EXPECT_FALSE(CupsConfig::parse(one(CupsConfig::kShare, value)).has_value())
            << value.toString().toStdString();
    }
}

TEST(ut_CupsConfig, rejectsOneBadEntryAmongGoodOnes)
{
    QVariantMap raw = one(CupsConfig::kShare, true);
    raw.insert(QStringLiteral("_evil"), true);
    EXPECT_FALSE(CupsConfig::parse(raw).has_value());
}

TEST(ut_CupsConfig, internetPrintingNeedsSharing)
{
    QVariantMap raw = one(CupsConfig::kShare, false);
    raw.insert(QString::fromLatin1(CupsConfig::kRemoteAny), true);
    QString error;
    EXPECT_FALSE(CupsConfig::parse(raw, &error).has_value());
    EXPECT_FALSE(error.isEmpty());

    raw.insert(QString::fromLatin1(CupsConfig::kShare), true);
    EXPECT_TRUE(CupsConfig::parse(raw).has_value());
}

TEST(ut_CupsConfig, resultIsSortedByKey)
{
    QVariantMap raw = one(CupsConfig::kUserCancelAny, true);
    raw.insert(QString::fromLatin1(CupsConfig::kDebugLogging), false);
    raw.insert(QString::fromLatin1(CupsConfig::kShare), true);
    const auto parsed = CupsConfig::parse(raw);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 3);
    EXPECT_LE((*parsed)[0].key, (*parsed)[1].key);
    EXPECT_LE((*parsed)[1].key, (*parsed)[2].key);
}

TEST(ut_CupsConfig, applyRefusesEmptyRequest)
{
    QString error;
    EXPECT_FALSE(CupsConfig::apply({}, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(ut_CupsConfig, acceptsTheDiscoverySwitchAlongWithTheOthers)
{
    EXPECT_STREQ(CupsConfig::kDiscovery, "printer-discovery-enabled");
    QVariantMap all;
    for (const char *key : {CupsConfig::kShare, CupsConfig::kRemoteAdmin, CupsConfig::kRemoteAny,
                            CupsConfig::kUserCancelAny, CupsConfig::kDebugLogging, CupsConfig::kDiscovery}) {
        all.insert(QString::fromLatin1(key), true);
    }
    const auto parsed = CupsConfig::parse(all);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->size(), 6);
    EXPECT_FALSE(CupsConfig::parse(one("printer-discovery-enabled", QStringLiteral("1"))).has_value());
}
