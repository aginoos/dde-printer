// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QUrl>

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
    EXPECT_TRUE(hp.contains("&deb_manufacturer=HP&desc=HP%20LaserJet%201020"));
    EXPECT_TRUE(hewlett.contains("&deb_manufacturer=HP&desc=HP%20LaserJet%201020"));
}

TEST(ut_PrinterService, otherManufacturersKeepTheirName)
{
    const QString url = urlOf("Canon", "LBP2900", QString());
    EXPECT_TRUE(url.contains("&deb_manufacturer=Canon&desc=Canon%20LBP2900"));
}

TEST(ut_PrinterService, deviceIdTravelsAsPercentEncodedBase64)
{
    const QString id = "MFG:Canon;MDL:LBP2900;";
    const QString url = urlOf("Canon", "LBP2900", id);
    const QString expected = PrinterService::encodeQueryValue(QString::fromLatin1(id.toUtf8().toBase64()));
    EXPECT_TRUE(url.endsWith("&ieee1284=" + expected));
    EXPECT_TRUE(urlOf("Canon", "LBP2900", QString()).endsWith("&ieee1284="));
}

TEST(ut_PrinterService, base64PaddingAndPlusAreEncoded)
{
    // "MFG:A;MDL:B~?>;" encodes to base64 with '+', '/' or '=' characters
    const QString url = urlOf("A", "B", "MFG:A;MDL:B~?>;");
    const QString tail = url.mid(url.indexOf("&ieee1284=") + 10);
    EXPECT_FALSE(tail.contains('='));
    EXPECT_FALSE(tail.contains('+'));
    EXPECT_FALSE(tail.contains('/'));
}

TEST(ut_PrinterService, serialNumberNeverLeavesTheMachine)
{
    const QString id = "MFG:EPSON;CMD:ESCPL2;MDL:ET-2820;SN:X1Y2Z3;SERN:ABC123;CLS:PRINTER;";
    const QString url = urlOf("EPSON", "ET-2820", id);
    const QString encoded = url.mid(url.indexOf("&ieee1284=") + 10);
    const QByteArray decoded = QByteArray::fromBase64(QUrl::fromPercentEncoding(encoded.toLatin1()).toLatin1());
    EXPECT_FALSE(decoded.contains("X1Y2Z3"));
    EXPECT_FALSE(decoded.contains("ABC123"));
    EXPECT_TRUE(decoded.contains("MDL:ET-2820"));
}

TEST(ut_PrinterService, privacySafeDeviceIdKeepsOnlyLookupFields)
{
    EXPECT_EQ(PrinterService::privacySafeDeviceId("MFG:HP;MDL:LaserJet 1020;CMD:ZJS;SN:123;"),
              QString("MFG:HP;MDL:LaserJet 1020;CMD:ZJS;"));
    EXPECT_EQ(PrinterService::privacySafeDeviceId("MANUFACTURER:Brother;MODEL:HL-L2350DW;COMMAND SET:PJL,PCL;SERIALNUMBER:U1;SERIAL:2;"),
              QString("MANUFACTURER:Brother;MODEL:HL-L2350DW;COMMAND SET:PJL,PCL;"));
}

TEST(ut_PrinterService, privacySafeDeviceIdIsCaseInsensitiveAndDropsUnknownFields)
{
    EXPECT_EQ(PrinterService::privacySafeDeviceId("mfg:Canon;sn:99;uuid:abc;mdl:LBP;vendorsecret:1;"),
              QString("MFG:Canon;MDL:LBP;"));
    EXPECT_EQ(PrinterService::privacySafeDeviceId("Sern:5;SN:6;"), QString());
}

TEST(ut_PrinterService, privacySafeDeviceIdHandlesMalformedInput)
{
    EXPECT_TRUE(PrinterService::privacySafeDeviceId("").isEmpty());
    EXPECT_TRUE(PrinterService::privacySafeDeviceId(";;;").isEmpty());
    EXPECT_TRUE(PrinterService::privacySafeDeviceId("no colon here").isEmpty());
    EXPECT_TRUE(PrinterService::privacySafeDeviceId(":value;").isEmpty());
    EXPECT_EQ(PrinterService::privacySafeDeviceId("MFG:A"), QString("MFG:A;"));   // missing trailing ';'
}

TEST(ut_PrinterService, deviceStringsCannotInjectQueryParameters)
{
    const QString url = urlOf("Evil&arch=x", "Model&system=y #frag", QString());
    EXPECT_TRUE(url.contains("deb_manufacturer=Evil%26arch%3Dx"));
    EXPECT_TRUE(url.contains("Model%26system%3Dy%20%23frag"));
    EXPECT_EQ(url.count("&arch="), 0);
    EXPECT_EQ(url.count("&system="), 0);
}

TEST(ut_PrinterService, encodeQueryValueEscapesReservedCharacters)
{
    EXPECT_EQ(PrinterService::encodeQueryValue("a b&c=d+e/f"), QString("a%20b%26c%3Dd%2Be%2Ff"));
    EXPECT_EQ(PrinterService::encodeQueryValue("Az09-._~"), QString("Az09-._~"));
}

TEST(ut_PrinterService, requestKeepsTheUrlAndArguments)
{
    PrinterServerInterface request("https://example.invalid/x", QJsonObject{{"k", 1}});
    EXPECT_EQ(request.m_url, QString("https://example.invalid/x"));
    EXPECT_EQ(request.m_args.value("k").toInt(), 1);
}
