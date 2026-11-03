// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "cupsconnectionfactory.h"

// Only failure paths: they never need a running CUPS server (loopback port 1 is closed).

TEST(ut_CupsConnectionFactory, emptyHostGivesNoConnection)
{
    EXPECT_EQ(CupsConnectionFactory::createConnection(QString(), 631), nullptr);
}

TEST(ut_CupsConnectionFactory, refusedConnectionIsReportedAsNull)
{
    // Connection::init throws for a refused connect; the factory must swallow it
    EXPECT_EQ(CupsConnectionFactory::createConnection(QStringLiteral("127.0.0.1"), 1), nullptr);
}
