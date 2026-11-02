// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <initializer_list>

// Included to reach the parsing helpers in the unnamed namespace.
#include "canonjob.cpp"

namespace {

QStringList strings(std::initializer_list<const char *> items)
{
    QStringList list;
    for (const char *item : items) {
        list << QString::fromLatin1(item);
    }
    return list;
}

}  // namespace

// ------------------------------------------------------- invalidSpoolNames

TEST(ut_CanonJob, invalidSpoolNamesTakesTheThirdColumn)
{
    const QByteArray listing =
        "Name  Port  Spool  State\n"
        "LBP6030 /dev/capt0 spool_a invalid Spool Name\n"
        "LBP2900 /dev/capt1 spool_b OK\n";
    EXPECT_EQ(invalidSpoolNames(listing), strings({"spool_a"}));
}

TEST(ut_CanonJob, invalidSpoolNamesCollectsEveryMarkedLine)
{
    const QByteArray listing =
        "a b one invalid Spool Name\n"
        "a b two invalid Spool Name\n"
        "a b three fine\n"
        "a b four invalid Spool Name";
    EXPECT_EQ(invalidSpoolNames(listing), strings({"one", "two", "four"}));
}

TEST(ut_CanonJob, invalidSpoolNamesNormalisesWhitespaceAndLineEndings)
{
    const QByteArray listing = "  a \t b\t\t spool_x   invalid   Spool Name \r\n";
    // "invalid   Spool Name" is only recognised with single spaces between the words
    EXPECT_TRUE(invalidSpoolNames(listing).isEmpty());

    const QByteArray spaced = "  a \t b\t\t spool_x invalid Spool Name \r\n";
    EXPECT_EQ(invalidSpoolNames(spaced), strings({"spool_x"}));
}

TEST(ut_CanonJob, invalidSpoolNamesDropsUnsafeNames)
{
    // names end up as arguments of "ccpdadmin -x": only valid queue names pass
    const QByteArray listing =
        "a b -rf invalid Spool Name\n"
        "a b ../x invalid Spool Name\n"
        "a b x;id invalid Spool Name\n"
        "a b ok_name invalid Spool Name\n";
    EXPECT_EQ(invalidSpoolNames(listing), strings({"ok_name"}));
}

TEST(ut_CanonJob, invalidSpoolNamesHandlesShortLinesAndEmptyInput)
{
    EXPECT_TRUE(invalidSpoolNames(QByteArray()).isEmpty());
    EXPECT_TRUE(invalidSpoolNames("just some text\n").isEmpty());
}

// ------------------------------------------------------------ isCcpQueue

TEST(ut_CanonJob, isCcpQueueMatchesOnlyCcpDevices)
{
    const QByteArray out =
        "device for Canon_LBP: ccp://localhost:59687\n"
        "device for Office: ipp://printer/ipp/print\n"
        "device for Usb: usb://Canon/LBP\n";
    EXPECT_TRUE(isCcpQueue(out, "Canon_LBP"));
    EXPECT_FALSE(isCcpQueue(out, "Office"));
    EXPECT_FALSE(isCcpQueue(out, "Usb"));
    EXPECT_FALSE(isCcpQueue(out, "Missing"));
}

TEST(ut_CanonJob, isCcpQueueNeedsTheWholeName)
{
    const QByteArray out = "device for Canon_LBP: ccp://localhost:59687\n";
    EXPECT_FALSE(isCcpQueue(out, "Canon"));        // prefix of another queue
    EXPECT_FALSE(isCcpQueue(out, "Canon_LBP2"));
    EXPECT_FALSE(isCcpQueue(out, ""));
    EXPECT_FALSE(isCcpQueue(QByteArray(), "Canon_LBP"));
}

TEST(ut_CanonJob, isCcpQueueIgnoresTextInTheMiddleOfALine)
{
    const QByteArray out = "note: device for Canon_LBP: ccp://localhost\n";
    EXPECT_FALSE(isCcpQueue(out, "Canon_LBP"));
}

// -------------------------------------------------------------- lpadminArgs

TEST(ut_CanonJob, lpadminArgsKeepTheQueuePrivate)
{
    CanonInstallRequest req;
    req.printerName = "LBP2900";
    req.deviceUri = "ccp://localhost:59687";
    req.ppdPath = "/usr/share/cups/model/CNCUPSLBP2900CAPTK.ppd";
    const QStringList args = lpadminArgs(req);
    EXPECT_EQ(args.mid(0, 2), strings({"-p", "LBP2900"}));
    EXPECT_TRUE(args.contains("-E"));
    EXPECT_EQ(args.mid(args.size() - 2), strings({"-o", "printer-is-shared=false"}));
}
