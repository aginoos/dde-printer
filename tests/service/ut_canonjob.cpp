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

// ------------------------------------------------- listingMentionsDevice

TEST(ut_CanonJobDevice, exactAddressMatches)
{
    EXPECT_TRUE(listingMentionsDevice("CNB LBP 192.168.1.20 ready\n", "192.168.1.20"));
    EXPECT_TRUE(listingMentionsDevice("192.168.1.20\n", "192.168.1.20"));
    EXPECT_TRUE(listingMentionsDevice("addr=192.168.1.20.", "192.168.1.20"));
}

TEST(ut_CanonJobDevice, addressInsideLongerAddressDoesNotMatch)
{
    EXPECT_FALSE(listingMentionsDevice("printer 11.1.1.10 ready\n", "1.1.1.1"));
    EXPECT_FALSE(listingMentionsDevice("printer 1.1.1.10 ready\n", "1.1.1.1"));
    EXPECT_FALSE(listingMentionsDevice("printer 21.1.1.1 ready\n", "1.1.1.1"));
    EXPECT_FALSE(listingMentionsDevice("printer 1.1.1.1.5 ready\n", "1.1.1.1"));
}

TEST(ut_CanonJobDevice, serialIsMatchedAsWholeWord)
{
    EXPECT_TRUE(listingMentionsDevice("dev: ABC123 online", "ABC123"));
    EXPECT_FALSE(listingMentionsDevice("dev: XABC123 online", "ABC123"));
    EXPECT_FALSE(listingMentionsDevice("dev: ABC1234 online", "ABC123"));
    EXPECT_FALSE(listingMentionsDevice("dev: ABC-123 online", "ABC"));
}

TEST(ut_CanonJobDevice, emptyTokenOrListingNeverMatches)
{
    EXPECT_FALSE(listingMentionsDevice("anything", ""));
    EXPECT_FALSE(listingMentionsDevice("", "1.2.3.4"));
}

TEST(ut_CanonJobDevice, tokenIsNotInterpretedAsPattern)
{
    EXPECT_FALSE(listingMentionsDevice("1x2x3x4", "1.2.3.4"));
}

// ------------------------------------------------------- queueExists

TEST(ut_CanonJobDevice, queueExistsDetectsAnyBackend)
{
    const QByteArray listing = "device for Office: ipp://host/ipp\ndevice for Canon1: ccp://localhost:59687\n";
    EXPECT_TRUE(queueExists(listing, "Office"));
    EXPECT_TRUE(queueExists(listing, "Canon1"));
    EXPECT_FALSE(queueExists(listing, "Canon"));
    EXPECT_FALSE(queueExists("", "Office"));
    EXPECT_TRUE(isCcpQueue(listing, "Canon1"));
    EXPECT_FALSE(isCcpQueue(listing, "Office"));
}
