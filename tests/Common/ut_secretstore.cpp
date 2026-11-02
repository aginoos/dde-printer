// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>

#include "secretstore.h"

// Only the parts that never reach the keyring are exercised: the tests must not depend on a
// running Secret Service nor touch the real keyring of whoever runs them.

TEST(ut_SecretStore, encodeDecodeRoundTrip)
{
    const SmbCredential credential {"alice", "p\"a:ss wörd", "CORP"};
    const std::optional<SmbCredential> decoded = SecretStore::decode(SecretStore::encode(credential));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->user, credential.user);
    EXPECT_EQ(decoded->password, credential.password);
    EXPECT_EQ(decoded->domain, credential.domain);
}

TEST(ut_SecretStore, encodeOmitsEmptyDomain)
{
    const QByteArray encoded = SecretStore::encode(SmbCredential {"bob", "secret", QString()});
    EXPECT_FALSE(encoded.contains("domain"));
    ASSERT_TRUE(SecretStore::decode(encoded).has_value());
}

TEST(ut_SecretStore, decodeRejectsBrokenOrIncompleteValues)
{
    EXPECT_FALSE(SecretStore::decode(QByteArray()).has_value());
    EXPECT_FALSE(SecretStore::decode("not json").has_value());
    EXPECT_FALSE(SecretStore::decode("[1,2]").has_value());
    EXPECT_FALSE(SecretStore::decode("{\"user\":\"u\"}").has_value());
    EXPECT_FALSE(SecretStore::decode("{\"password\":\"p\"}").has_value());
}

TEST(ut_SecretStore, usableNeedsUserAndPassword)
{
    EXPECT_TRUE((SmbCredential {"u", "p", QString()}).isUsable());
    EXPECT_FALSE((SmbCredential {"u", QString(), QString()}).isUsable());
    EXPECT_FALSE((SmbCredential {QString(), "p", QString()}).isUsable());
}

TEST(ut_SecretStore, rejectsWithoutTouchingTheKeyring)
{
    const SmbCredential usable {"u", "p", QString()};
    EXPECT_FALSE(SecretStore::storeSmb("ipp://srv/share", usable));
    EXPECT_FALSE(SecretStore::storeSmb("smb://srv", usable));
    EXPECT_FALSE(SecretStore::storeSmb("smb://srv/share", SmbCredential {"u", QString(), QString()}));
    EXPECT_FALSE(SecretStore::lookupSmb("ipp://srv/share").has_value());
    EXPECT_FALSE(SecretStore::removeSmb(QString()));
}

TEST(ut_SecretStore, adoptKeepsUrisWithoutPasswordUnchanged)
{
    EXPECT_EQ(SecretStore::adoptUriCredentials("smb://srv/share"), QString("smb://srv/share"));
    EXPECT_EQ(SecretStore::adoptUriCredentials("smb://nobody@srv/share"), QString("smb://nobody@srv/share"));
    EXPECT_EQ(SecretStore::adoptUriCredentials("ipp://u:p@srv/x"), QString("ipp://u:p@srv/x"));
    EXPECT_EQ(SecretStore::adoptUriCredentials(QString()), QString());
}

TEST(ut_SecretStore, forgetIgnoresOtherUris)
{
    SecretStore::forgetPrinter("ipp://srv/printers/x");
    SecretStore::forgetPrinter(QString());
    SUCCEED();
}
