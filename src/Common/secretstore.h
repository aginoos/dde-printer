// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SECRETSTORE_H
#define SECRETSTORE_H

#include <QByteArray>
#include <QString>

#include <functional>
#include <optional>

struct SmbCredential
{
    QString user;
    QString password;
    QString domain;

    bool isUsable() const { return !user.isEmpty() && !password.isEmpty(); }
};

// Remembered passwords of Windows (SMB) shared printers, kept in the session keyring
// (Secret Service through libsecret). Nothing is ever written to a file, the log or a command line.
// When no keyring is reachable every call fails softly: the password is simply not remembered.
class SecretStore
{
public:
    // Serialisation of the secret value (public for tests).
    static QByteArray encode(const SmbCredential &credential);
    static std::optional<SmbCredential> decode(const QByteArray &value);

    // `uri` is the printer's device URI; it is reduced to SmbUri::credentialKey().
    // Returns false when the URI is not an smb share, the credential is incomplete or the keyring failed.
    static bool storeSmb(const QString &uri, const SmbCredential &credential);
    static std::optional<SmbCredential> lookupSmb(const QString &uri);
    static bool removeSmb(const QString &uri);

    // Deleting a printer: forgets its password when it is an smb share, ignores everything else.
    static void forgetPrinter(const QString &deviceUri);

    // A password typed into an smb:// URI (smb://user:password@host/share) is moved into the keyring.
    // Returns the URI without user name and password; other URIs are returned unchanged.
    static QString adoptUriCredentials(const QString &uri);

    // Seam for tests: the three keyring operations behind the calls above. The default backend
    // talks to libsecret; tests install a fake so that no real keyring is touched.
    // `isCancelled` turns true when the deadline of the call has passed.
    struct KeyringResult
    {
        bool ok = false;
        bool cancelled = false;   // the call was aborted by the deadline (no usable keyring)
    };
    struct StoreRequest
    {
        QByteArray key, host, share, user, label, secret;
    };
    using CancelProbe = std::function<bool()>;
    struct Backend
    {
        std::function<KeyringResult(const QByteArray &key, const CancelProbe &isCancelled)> clear;
        std::function<KeyringResult(const StoreRequest &request, const CancelProbe &isCancelled)> store;
        std::function<std::optional<QByteArray>(const QByteArray &key, const CancelProbe &isCancelled)> lookup;
    };
    static void setBackend(const Backend &backend);   // an empty member keeps the libsecret default
    static void resetBackend();
    static void setTimeoutMs(int timeoutMs);          // default 8000

private:
    SecretStore() = delete;
};

#endif  // SECRETSTORE_H
