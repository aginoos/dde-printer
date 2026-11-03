// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QList>
#include <QString>
#include <QThread>

#include "secretstore.h"
#include "smburi.h"

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

namespace {

// Fake keyring: records calls, never touches libsecret.
struct FakeKeyring
{
    QList<QByteArray> clearedKeys;
    QList<SecretStore::StoreRequest> stores;
    QList<QByteArray> lookups;
    SecretStore::KeyringResult clearResult {true, false};
    SecretStore::KeyringResult storeResult {true, false};
    std::optional<QByteArray> lookupValue;

    FakeKeyring()
    {
        SecretStore::Backend backend;
        backend.clear = [this](const QByteArray &key, const SecretStore::CancelProbe &) {
            clearedKeys.append(key);
            return clearResult;
        };
        backend.store = [this](const SecretStore::StoreRequest &request, const SecretStore::CancelProbe &) {
            stores.append(request);
            return storeResult;
        };
        backend.lookup = [this](const QByteArray &key, const SecretStore::CancelProbe &) {
            lookups.append(key);
            return lookupValue;
        };
        SecretStore::setBackend(backend);
    }
    ~FakeKeyring() { SecretStore::resetBackend(); }
};

}  // namespace

TEST(ut_SecretStore, storeClearsOldEntryThenStoresWithAttributes)
{
    FakeKeyring keyring;
    ASSERT_TRUE(SecretStore::storeSmb("smb://WG/Srv/Share", SmbCredential {"alice", "pw", "CORP"}));
    ASSERT_EQ(keyring.clearedKeys.size(), 1);
    ASSERT_EQ(keyring.stores.size(), 1);
    const SecretStore::StoreRequest &r = keyring.stores.first();
    EXPECT_EQ(r.key, keyring.clearedKeys.first());
    EXPECT_EQ(r.key, SmbUri::credentialKey("smb://WG/Srv/Share").toUtf8());
    EXPECT_EQ(r.host, QByteArray("Srv"));
    EXPECT_EQ(r.share, QByteArray("share"));
    EXPECT_EQ(r.user, QByteArray("alice"));
    EXPECT_TRUE(r.label.contains("Srv/Share"));
}

TEST(ut_SecretStore, storeCarriesEncodedSecret)
{
    FakeKeyring keyring;
    QByteArray seen;
    SecretStore::Backend backend;
    backend.clear = [](const QByteArray &, const SecretStore::CancelProbe &) { return SecretStore::KeyringResult {true, false}; };
    backend.store = [&seen](const SecretStore::StoreRequest &request, const SecretStore::CancelProbe &) {
        seen = request.secret;   // copy before the store wipes the original
        return SecretStore::KeyringResult {true, false};
    };
    SecretStore::setBackend(backend);
    ASSERT_TRUE(SecretStore::storeSmb("smb://srv/share", SmbCredential {"u", "p", QString()}));
    EXPECT_EQ(seen, SecretStore::encode(SmbCredential {"u", "p", QString()}));
}

TEST(ut_SecretStore, cancelledClearSkipsTheStore)
{
    FakeKeyring keyring;
    keyring.clearResult = {false, true};
    EXPECT_FALSE(SecretStore::storeSmb("smb://srv/share", SmbCredential {"u", "p", QString()}));
    EXPECT_EQ(keyring.stores.size(), 0);
}

TEST(ut_SecretStore, failedClearStillStores)
{
    FakeKeyring keyring;
    keyring.clearResult = {false, false};
    EXPECT_TRUE(SecretStore::storeSmb("smb://srv/share", SmbCredential {"u", "p", QString()}));
    EXPECT_EQ(keyring.stores.size(), 1);
}

TEST(ut_SecretStore, storeFailureIsReported)
{
    FakeKeyring keyring;
    keyring.storeResult = {false, false};
    EXPECT_FALSE(SecretStore::storeSmb("smb://srv/share", SmbCredential {"u", "p", QString()}));
}

TEST(ut_SecretStore, lookupDecodesStoredValue)
{
    FakeKeyring keyring;
    keyring.lookupValue = SecretStore::encode(SmbCredential {"bob", "pw", "D"});
    const std::optional<SmbCredential> found = SecretStore::lookupSmb("smb://srv/Share");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->user, QString("bob"));
    EXPECT_EQ(found->domain, QString("D"));
    ASSERT_EQ(keyring.lookups.size(), 1);
    EXPECT_EQ(keyring.lookups.first(), SmbUri::credentialKey("smb://srv/Share").toUtf8());
}

TEST(ut_SecretStore, lookupMissingOrUndecodable)
{
    FakeKeyring keyring;
    EXPECT_FALSE(SecretStore::lookupSmb("smb://srv/share").has_value());
    keyring.lookupValue = QByteArray("garbage");
    EXPECT_FALSE(SecretStore::lookupSmb("smb://srv/share").has_value());
    keyring.lookupValue = QByteArray("{\"user\":\"u\"}");
    EXPECT_FALSE(SecretStore::lookupSmb("smb://srv/share").has_value());
}

TEST(ut_SecretStore, removeReportsBackendResult)
{
    FakeKeyring keyring;
    EXPECT_TRUE(SecretStore::removeSmb("smb://srv/share"));
    keyring.clearResult = {false, false};
    EXPECT_FALSE(SecretStore::removeSmb("smb://srv/share"));
    EXPECT_EQ(keyring.clearedKeys.size(), 2);
}

TEST(ut_SecretStore, forgetPrinterRemovesOnlySmbEntries)
{
    FakeKeyring keyring;
    SecretStore::forgetPrinter("ipp://srv/printers/x");
    EXPECT_TRUE(keyring.clearedKeys.isEmpty());
    SecretStore::forgetPrinter("smb://srv/share");
    ASSERT_EQ(keyring.clearedKeys.size(), 1);
    EXPECT_EQ(keyring.clearedKeys.first(), SmbUri::credentialKey("smb://srv/share").toUtf8());
}

TEST(ut_SecretStore, adoptMovesPasswordIntoKeyring)
{
    FakeKeyring keyring;
    const QString clean = SecretStore::adoptUriCredentials("smb://alice:pw@srv/share");
    EXPECT_FALSE(clean.contains("pw"));
    EXPECT_FALSE(clean.contains("alice"));
    ASSERT_EQ(keyring.stores.size(), 1);
    EXPECT_EQ(keyring.stores.first().user, QByteArray("alice"));
}

TEST(ut_SecretStore, adoptStripsUriEvenWhenKeyringFails)
{
    FakeKeyring keyring;
    keyring.clearResult = {false, true};
    const QString clean = SecretStore::adoptUriCredentials("smb://alice:pw@srv/share");
    EXPECT_EQ(clean, SmbUri::stripCredentials("smb://alice:pw@srv/share"));
    EXPECT_EQ(keyring.stores.size(), 0);
}

TEST(ut_SecretStore, deadlineCancelsSlowKeyringCall)
{
    FakeKeyring keyring;
    SecretStore::setTimeoutMs(50);
    bool sawCancel = false;
    SecretStore::Backend backend;
    backend.clear = [&sawCancel](const QByteArray &, const SecretStore::CancelProbe &isCancelled) {
        for (int i = 0; i < 400 && !isCancelled(); ++i)
            QThread::msleep(5);
        sawCancel = isCancelled();
        return SecretStore::KeyringResult {false, sawCancel};
    };
    bool stored = false;
    backend.store = [&stored](const SecretStore::StoreRequest &, const SecretStore::CancelProbe &) {
        stored = true;
        return SecretStore::KeyringResult {true, false};
    };
    SecretStore::setBackend(backend);
    EXPECT_FALSE(SecretStore::storeSmb("smb://srv/share", SmbCredential {"u", "p", QString()}));
    EXPECT_TRUE(sawCancel);
    EXPECT_FALSE(stored);
}

TEST(ut_SecretStore, probeStaysFalseBeforeDeadline)
{
    FakeKeyring keyring;
    bool cancelledEarly = true;
    SecretStore::Backend backend;
    backend.lookup = [&cancelledEarly](const QByteArray &, const SecretStore::CancelProbe &isCancelled) {
        cancelledEarly = isCancelled();
        return std::optional<QByteArray>();
    };
    SecretStore::setBackend(backend);
    SecretStore::lookupSmb("smb://srv/share");
    EXPECT_FALSE(cancelledEarly);
}
