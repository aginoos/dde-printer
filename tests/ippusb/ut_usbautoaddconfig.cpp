// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QVariant>

#include "usbautoaddconfig.h"

TEST(ut_UsbAutoAddConfig, interpretDefaultsToEnabled)
{
    EXPECT_TRUE(UsbAutoAddConfig::interpret(QVariant()));
    EXPECT_TRUE(UsbAutoAddConfig::interpret(QVariant("false")));   // not a bool: ignore
    EXPECT_TRUE(UsbAutoAddConfig::interpret(QVariant(0)));
}

TEST(ut_UsbAutoAddConfig, interpretHonoursBooleans)
{
    EXPECT_TRUE(UsbAutoAddConfig::interpret(QVariant(true)));
    EXPECT_FALSE(UsbAutoAddConfig::interpret(QVariant(false)));
}

TEST(ut_UsbAutoAddConfig, keyMatchesMetadata)
{
    EXPECT_STREQ(UsbAutoAddConfig::kKey, "allow_auto_usb");
}
