// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "driverinstallconfirm.h"

TEST(ut_DriverInstallConfirm, describeShowsVersionWhenKnown)
{
    EXPECT_EQ(DriverInstallConfirm::describe("hplip", "3.23.12-1"), QString("hplip (3.23.12-1)"));
    EXPECT_EQ(DriverInstallConfirm::describe("hplip", QString()), QString("hplip"));
}

TEST(ut_DriverInstallConfirm, handlerDecidesAndReceivesTheDescriptions)
{
    QStringList seen;
    DriverInstallConfirm::setHandler([&seen](const QStringList &lines) {
        seen = lines;
        return true;
    });
    EXPECT_TRUE(DriverInstallConfirm::confirm({"a (1)", "b"}));
    EXPECT_EQ(seen, (QStringList{"a (1)", "b"}));

    DriverInstallConfirm::setHandler([](const QStringList &) { return false; });
    EXPECT_FALSE(DriverInstallConfirm::confirm({"a"}));
    DriverInstallConfirm::setHandler(nullptr);
}

TEST(ut_DriverInstallConfirm, nothingToInstallIsNeverConfirmed)
{
    DriverInstallConfirm::setHandler([](const QStringList &) { return true; });
    EXPECT_FALSE(DriverInstallConfirm::confirm({}));
    DriverInstallConfirm::setHandler(nullptr);
}

TEST(ut_DriverInstallConfirm, withoutAGuiApplicationNobodyAgrees)
{
    // The test binary runs a QCoreApplication, so the default dialog path must refuse.
    DriverInstallConfirm::setHandler(nullptr);
    EXPECT_FALSE(DriverInstallConfirm::confirm({"hplip (3.23)"}));
}
