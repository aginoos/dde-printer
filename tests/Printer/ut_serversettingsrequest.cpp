// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serversettingsrequest.h"

#include <cups/adminutil.h>

#include <gtest/gtest.h>

using Options = std::map<std::string, std::string>;

TEST(ut_ServerSettingsRequest, convertsSwitchesToBooleans)
{
    const Options options = {{CUPS_SERVER_SHARE_PRINTERS, "1"}, {CUPS_SERVER_REMOTE_ANY, "0"}};
    const auto map = ServerSettingsRequest::toVariantMap(options);
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(map->size(), 2);
    EXPECT_EQ(map->value(CUPS_SERVER_SHARE_PRINTERS).metaType().id(), QMetaType::Bool);
    EXPECT_TRUE(map->value(CUPS_SERVER_SHARE_PRINTERS).toBool());
    EXPECT_FALSE(map->value(CUPS_SERVER_REMOTE_ANY).toBool());
}

TEST(ut_ServerSettingsRequest, acceptsAllKnownSwitches)
{
    const Options options = {{CUPS_SERVER_SHARE_PRINTERS, "1"}, {CUPS_SERVER_REMOTE_ADMIN, "1"},
                             {CUPS_SERVER_REMOTE_ANY, "1"}, {CUPS_SERVER_USER_CANCEL_ANY, "0"},
                             {CUPS_SERVER_DEBUG_LOGGING, "0"}};
    EXPECT_TRUE(ServerSettingsRequest::toVariantMap(options).has_value());
}

TEST(ut_ServerSettingsRequest, refusesEmpty)
{
    QString error;
    EXPECT_FALSE(ServerSettingsRequest::toVariantMap({}, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(ut_ServerSettingsRequest, refusesUnknownName)
{
    QString error;
    EXPECT_FALSE(ServerSettingsRequest::toVariantMap({{"LogLevel", "1"}}, &error).has_value());
    EXPECT_FALSE(error.isEmpty());
}

TEST(ut_ServerSettingsRequest, refusesNonBinaryValue)
{
    for (const char *value : {"", "2", "yes", "1\nPermitRoot yes", "true"}) {
        EXPECT_FALSE(ServerSettingsRequest::toVariantMap({{CUPS_SERVER_SHARE_PRINTERS, value}}).has_value())
            << value;
    }
}
