// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>

#include <cups/ipp.h>

#include "smburi.h"

TEST(ut_SmbUri, parseServerAndShare)
{
    const SmbUriParts parts = SmbUri::parse("smb://server/share");
    ASSERT_TRUE(parts.isValid());
    EXPECT_EQ(parts.server, QString("server"));
    EXPECT_EQ(parts.share, QString("share"));
    EXPECT_TRUE(parts.workgroup.isEmpty());
    EXPECT_FALSE(parts.hasCredentials());
}

TEST(ut_SmbUri, parseWorkgroupServerShare)
{
    const SmbUriParts parts = SmbUri::parse("smb://WG/server/share");
    EXPECT_EQ(parts.workgroup, QString("WG"));
    EXPECT_EQ(parts.server, QString("server"));
    EXPECT_EQ(parts.share, QString("share"));
}

TEST(ut_SmbUri, parseDecodesCredentialsAndShare)
{
    const SmbUriParts parts = SmbUri::parse("smb://user:p%40ss%3Aw@wg/server/My%20Printer");
    EXPECT_EQ(parts.user, QString("user"));
    EXPECT_EQ(parts.password, QString("p@ss:w"));
    EXPECT_EQ(parts.workgroup, QString("wg"));
    EXPECT_EQ(parts.server, QString("server"));
    EXPECT_EQ(parts.share, QString("My Printer"));
    EXPECT_TRUE(parts.hasCredentials());
}

TEST(ut_SmbUri, parseUserWithoutPassword)
{
    const SmbUriParts parts = SmbUri::parse("smb://nobody@server/share");
    EXPECT_EQ(parts.user, QString("nobody"));
    EXPECT_TRUE(parts.password.isEmpty());
    EXPECT_EQ(parts.server, QString("server"));
}

TEST(ut_SmbUri, parseServerOnlyAndQuery)
{
    EXPECT_TRUE(SmbUri::parse("smb://server").share.isEmpty());
    EXPECT_EQ(SmbUri::parse("smb://server/share?encoding=utf8").share, QString("share"));
}

TEST(ut_SmbUri, parseRejectsOtherSchemesAndEmptyServer)
{
    EXPECT_FALSE(SmbUri::parse("ipp://server/share").isValid());
    EXPECT_FALSE(SmbUri::parse("smb:///").isValid());
    EXPECT_FALSE(SmbUri::parse(QString()).isValid());
    EXPECT_TRUE(SmbUri::parse("SMB://Server/Share").isValid());
}

TEST(ut_SmbUri, buildEncodesEverythingButTheServer)
{
    SmbUriParts parts;
    parts.workgroup = "WG";
    parts.server = "srv";
    parts.share = "HP LaserJet";
    EXPECT_EQ(SmbUri::build(parts, false), QString("smb://WG/srv/HP%20LaserJet"));

    parts.workgroup.clear();
    parts.user = "a b";
    parts.password = "p:w";
    EXPECT_EQ(SmbUri::build(parts, true), QString("smb://a%20b:p%3Aw@srv/HP%20LaserJet"));
    EXPECT_EQ(SmbUri::build(parts, false), QString("smb://srv/HP%20LaserJet"));
}

TEST(ut_SmbUri, buildUserOnlyAndInvalid)
{
    SmbUriParts parts;
    parts.server = "srv";
    parts.share = "p";
    parts.user = "nobody";
    EXPECT_EQ(SmbUri::build(parts, true), QString("smb://nobody@srv/p"));
    EXPECT_TRUE(SmbUri::build(SmbUriParts(), true).isEmpty());
}

TEST(ut_SmbUri, stripCredentialsRemovesUserAndPassword)
{
    EXPECT_EQ(SmbUri::stripCredentials("smb://u:p@srv/sh"), QString("smb://srv/sh"));
    EXPECT_EQ(SmbUri::stripCredentials("smb://u:p@wg/srv/My%20P"), QString("smb://wg/srv/My%20P"));
}

TEST(ut_SmbUri, stripCredentialsLeavesOtherInputUntouched)
{
    EXPECT_EQ(SmbUri::stripCredentials("smb://srv/sh%20x"), QString("smb://srv/sh%20x"));
    EXPECT_EQ(SmbUri::stripCredentials("ipp://u:p@srv/x"), QString("ipp://u:p@srv/x"));
}

TEST(ut_SmbUri, withGuestUserReplacesCredentials)
{
    EXPECT_EQ(SmbUri::withGuestUser("smb://srv/sh"), QString("smb://nobody@srv/sh"));
    EXPECT_EQ(SmbUri::withGuestUser("smb://u:p@srv/sh"), QString("smb://nobody@srv/sh"));
    EXPECT_EQ(SmbUri::withGuestUser("ipp://srv/sh"), QString("ipp://srv/sh"));
}

TEST(ut_SmbUri, credentialKeyIsCanonical)
{
    EXPECT_EQ(SmbUri::credentialKey("smb://u:p@WG/SRV/My%20Printer"), QString("smb://wg/srv/my printer"));
    EXPECT_EQ(SmbUri::credentialKey("smb://srv/Sh"), SmbUri::credentialKey("smb://SRV/sh"));
    EXPECT_EQ(SmbUri::credentialKey("smb://nobody@srv/sh"), SmbUri::credentialKey("smb://srv/sh"));
}

TEST(ut_SmbUri, credentialKeyNeedsServerAndShare)
{
    EXPECT_TRUE(SmbUri::credentialKey("smb://srv").isEmpty());
    EXPECT_TRUE(SmbUri::credentialKey("ipp://srv/sh").isEmpty());
    EXPECT_TRUE(SmbUri::credentialKey(QString()).isEmpty());
}

TEST(ut_SmbUri, normalizeHostAcceptsCommonInput)
{
    EXPECT_EQ(SmbUri::normalizeHost(" 192.168.1.5 "), QString("192.168.1.5"));
    EXPECT_EQ(SmbUri::normalizeHost("\\\\printsrv"), QString("printsrv"));
    EXPECT_EQ(SmbUri::normalizeHost("smb://host/"), QString("host"));
    EXPECT_EQ(SmbUri::normalizeHost("host.example.com"), QString("host.example.com"));
    EXPECT_EQ(SmbUri::normalizeHost("my_host-1"), QString("my_host-1"));
    EXPECT_EQ(SmbUri::normalizeHost("[fe80::1]"), QString("[fe80::1]"));
}

TEST(ut_SmbUri, normalizeHostRejectsUnsafeInput)
{
    EXPECT_TRUE(SmbUri::normalizeHost(QString()).isEmpty());
    EXPECT_TRUE(SmbUri::normalizeHost("   ").isEmpty());
    EXPECT_TRUE(SmbUri::normalizeHost("bad host").isEmpty());
    EXPECT_TRUE(SmbUri::normalizeHost("host/share").isEmpty());
    EXPECT_TRUE(SmbUri::normalizeHost("user@host").isEmpty());
    EXPECT_TRUE(SmbUri::normalizeHost("-host").isEmpty());
    EXPECT_TRUE(SmbUri::normalizeHost("fe80::1").isEmpty());
    EXPECT_TRUE(SmbUri::normalizeHost(QString(300, 'a')).isEmpty());
}

TEST(ut_SmbJob, authenticationHoldNeedsHeldStateAndHint)
{
    EXPECT_TRUE(SmbJob::isAuthenticationHold(IPP_JSTATE_HELD, "Authentication is required for job 5."));
    EXPECT_TRUE(SmbJob::isAuthenticationHold(IPP_JSTATE_HELD, "job-hold-until-specified cups-held-for-authentication"));
    EXPECT_TRUE(SmbJob::isAuthenticationHold(IPP_JSTATE_HELD, "x authenticated"));
    EXPECT_FALSE(SmbJob::isAuthenticationHold(IPP_JSTATE_PENDING, "Authentication is required"));
    EXPECT_FALSE(SmbJob::isAuthenticationHold(IPP_JSTATE_HELD, "job-hold-until-specified"));
    EXPECT_FALSE(SmbJob::isAuthenticationHold(IPP_JSTATE_HELD, QString()));
}

TEST(ut_SmbJob, printerNameFromUri)
{
    EXPECT_EQ(SmbJob::printerNameFromUri("ipp://localhost/printers/HP-LaserJet"), QString("HP-LaserJet"));
    EXPECT_EQ(SmbJob::printerNameFromUri("ipp://localhost:631/printers/My%20Printer"), QString("My Printer"));
    EXPECT_EQ(SmbJob::printerNameFromUri("ipp://localhost/printers/A/extra"), QString("A"));
    EXPECT_TRUE(SmbJob::printerNameFromUri("ipp://localhost/jobs/1").isEmpty());
    EXPECT_TRUE(SmbJob::printerNameFromUri(QString()).isEmpty());
}
