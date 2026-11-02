// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <memory>

#include "vendor/printerservice.h"

namespace {

QString urlOf(const QString &manufacturer, const QString &model, const QString &id)
{
    std::unique_ptr<PrinterServerInterface> request(
        PrinterService::getInstance()->searchDriverSolution(manufacturer, model, id));
    return request ? request->m_url : QString();
}

}  // namespace

TEST(ut_PrinterService, singletonIsStable)
{
    EXPECT_EQ(PrinterService::getInstance(), PrinterService::getInstance());
}

TEST(ut_PrinterService, hpManufacturerNamesAreNormalised)
{
    const QString hp = urlOf("HP", "LaserJet 1020", QString());
    const QString hewlett = urlOf("Hewlett-Packard", "LaserJet 1020", QString());
    EXPECT_TRUE(hp.contains("&deb_manufacturer=HP&desc=HP LaserJet 1020"));
    EXPECT_TRUE(hewlett.contains("&deb_manufacturer=HP&desc=HP LaserJet 1020"));
}

TEST(ut_PrinterService, otherManufacturersKeepTheirName)
{
    const QString url = urlOf("Canon", "LBP2900", QString());
    EXPECT_TRUE(url.contains("&deb_manufacturer=Canon&desc=Canon LBP2900"));
}

TEST(ut_PrinterService, deviceIdTravelsAsBase64)
{
    const QString id = "MFG:Canon;MDL:LBP2900;";
    const QString url = urlOf("Canon", "LBP2900", id);
    EXPECT_TRUE(url.endsWith("&ieee1284=" + QString::fromLatin1(id.toUtf8().toBase64())));
    EXPECT_TRUE(urlOf("Canon", "LBP2900", QString()).endsWith("&ieee1284="));
}

TEST(ut_PrinterService, requestKeepsTheUrlAndArguments)
{
    PrinterServerInterface request("https://example.invalid/x", QJsonObject{{"k", 1}});
    EXPECT_EQ(request.m_url, QString("https://example.invalid/x"));
    EXPECT_EQ(request.m_args.value("k").toInt(), 1);
}
