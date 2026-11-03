// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QtGlobal>

#include "pluginstatus.h"

TEST(ut_PluginStatus, speaksUpOnlyWhenTheMetadataIsInstalledAndNobodyOwnsTheName)
{
    EXPECT_TRUE(PluginStatus::needsRelogin(true, false));    // installed, not loaded: log in again
    EXPECT_FALSE(PluginStatus::needsRelogin(true, true));    // loaded: nothing to say
    EXPECT_FALSE(PluginStatus::needsRelogin(false, false));  // no plugin shipped: nothing to wait for
    EXPECT_FALSE(PluginStatus::needsRelogin(false, true));
}

TEST(ut_PluginStatus, theServiceNameIsTheOneThePluginRegisters)
{
    EXPECT_STREQ(PluginStatus::kPluginService, "org.deepin.ddeprinter");
}

TEST(ut_PluginStatus, withoutASessionBusNothingIsRegistered)
{
    // Meant for the test environment, which has no desktop session: asking must not crash and must say
    // "not registered". On a machine with a session the answer depends on what is running.
    if (!qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS"))
        GTEST_SKIP() << "a session bus is available here";
    EXPECT_FALSE(PluginStatus::serviceRegistered());
}
