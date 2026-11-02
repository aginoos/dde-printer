// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "ippusbswitch.h"

// Only the refusal paths run here: the successful ones need a real ipp-usb
// printer and the avahi daemon.

TEST(ut_IppUsbSwitch, driverlessWithoutPublishedServiceFails)
{
    const IppUsbSwitch::Result result = IppUsbSwitch::driverlessUri("ipp://localhost:60999/ipp/print");
    if (result.ok)
        GTEST_SKIP() << "an ipp-usb service is really published on this machine";
    EXPECT_TRUE(result.uri.isEmpty());
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(ut_IppUsbSwitch, driverlessWithGarbageUriFails)
{
    const IppUsbSwitch::Result result = IppUsbSwitch::driverlessUri(QString());
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.uri.isEmpty());
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(ut_IppUsbSwitch, usbBackendForAnUnlistedModelFailsBeforeTouchingTheBlacklist)
{
    const IppUsbSwitch::Result result = IppUsbSwitch::usbBackendUri("Ut Zz Nonexistent Model 000");
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.uri.isEmpty());
    EXPECT_FALSE(result.error.isEmpty());
}
